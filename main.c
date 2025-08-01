#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

struct instruction {
    uint8_t bytes[15];
    int length;
};

enum TOKENTYPE {
    VALUE,
    OPERATOR,
    PARENTHESES_OPEN,
    PARENTHESES_CLOSE,
    NAME
};

struct token {
    enum TOKENTYPE type;
    uint64_t value;
};

enum OPERATORTYPE {
    MUL,
    ADD,
    SUB,
    DIV
};

enum INSTRUCTIONTYPE {
    POPR1,
    POPR2,
    PUSHR1,
    PUSHARG,
    PUSHCONST,
    MULR,
    ADDR,
    SUBR,
    DIVR,
    MOVABSR1,
    RET
};

struct instruction instructions[] = {
    {{0x58}, 1},                   // pop %rax
    {{0x5B}, 1},                   // pop %rbx
    {{0x50}, 1},                   // push %rax
    {{0x57}, 1},                   // push %rdi
    {{0x68, 0, 0, 0, 0}, 5},       // push const
    {{0x48, 0xF7, 0xE3}, 3},       // mul %rbx
    {{0x48, 0x01, 0xD8}, 3},       // add %rbx, %rax
    {{0x48, 0x29, 0xD8}, 3},       // sub %rbx, %rax
    {{0x48, 0xF7, 0xF3}, 3},       // div %rbx
    {{0x48, 0xb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}, 10}, // movabs const, reg
    {{0xc3}, 1}                    // ret
};

uint8_t doOperation[] = {
    0x5b,
    0x58,
    0x48, 0x00, 0x0, // The 0 bytes can be replaced by the appropriate operation.
    0x50
};

typedef int64_t (*f_x)(int);

bool isName(char* input, int* length) {
    *length = 0;
    if (isdigit(*input)) return false;
    if (!(isalnum(*input) || *input == '_')) return false;
    input++;
    (*length)++;
    while (isalnum(*input) || *input == '_') {
        input++;
        (*length)++;
    }
    return true;
}

bool isOperator(char* input, int* length) {
    *length = 0;
    if (*input < '*' || *input > '/') return false; // all operators I test for are in this range
    if (*input == ',' || *input == '.') return false; // the only non-operator outliers in the range
    *length = 1;
    return true;
}

bool isValue(char* input, int* length) {
    *length = 0;
    if (!isdigit(*input)) return false;
    input++;
    (*length)++;
    while (isdigit(*input)) {
        input++;
        (*length)++;
    }
    return true;
}

int tokenizeString(char* input, struct token* tokens, int maxTokenLength) {
    int index = 0;
    int length;
    int64_t wholeValue;
    int64_t fractionalValue;
    int fractionalLength;
    int operator;
    int denominator;
    while (*input != '\0') {
        if (isspace(*input)) {
            input++;
            continue;
        }
        if (*input == '(') {
            if (index == maxTokenLength) return -1;
            if (tokens[index-1].type == VALUE) {
                tokens[index++] = (struct token) {OPERATOR, MUL};
            }
            tokens[index++] = (struct token) {PARENTHESES_OPEN, 0};
            input += 1;
        }
        else if (*input == ')') {
            if (index == maxTokenLength) return -1;
            tokens[index++] = (struct token) {PARENTHESES_CLOSE, 0};
            input += 1;
        }
        else if (isName(input, &length)) {
            // for now, only support x and assume all valid names are x.
            if (index == maxTokenLength) return -1;
            if (tokens[index-1].type == VALUE) {
                tokens[index++] = (struct token) {OPERATOR, MUL};
            }
            tokens[index++] = (struct token) {NAME, 0};
            input += length;
        }
        else if (isOperator(input, &length)) {
            if (index == maxTokenLength) return -1;
            operator = *input;
            if (operator >= '/') 
                operator--;
            if (operator >= '-') 
                operator -= 1;
            operator -= '*';
            tokens[index++] = (struct token) {OPERATOR, operator};
            input += length;
        }
        else if (isValue(input, &length)) {
            if (index == maxTokenLength) return -1;
            wholeValue = atoll(input);
            input += length;
            if (*input == '.') {
                denominator = 1;
                input++;
                if (!isValue(input, &length)) return -1;
                fractionalValue = atoll(input);
                input += length;
                fractionalLength = length;
                while (fractionalLength) {
                    wholeValue *= 10;
                    denominator *= 10;
                    fractionalLength--;
                }
                wholeValue += fractionalValue;
                if (maxTokenLength - index < 5) return -1;
                tokens[index++] = (struct token) {PARENTHESES_OPEN, 0};
                tokens[index++] = (struct token) {VALUE, wholeValue};
                tokens[index++] = (struct token) {OPERATOR, DIV};
                tokens[index++] = (struct token) {VALUE, denominator};
                tokens[index++] = (struct token) {PARENTHESES_CLOSE, 0};
            }
            else {
                tokens[index++] = (struct token) {VALUE, wholeValue};
            }
        }
        else return -1;
    }
    return index;
}

void* commonBuffer;

enum equality {
    LESSER = -1,
    EQUAL,
    GREATER
};

enum equality compareOrder(struct token operator1, struct token operator2) {
    int firstOrder = operator1.value == MUL || operator1.value == DIV;
    int secondOrder = operator2.value == MUL || operator2.value == DIV;
    return (firstOrder < secondOrder) - (secondOrder < firstOrder);
}

uint8_t* copyOperation(uint8_t* outBuffer, enum OPERATORTYPE operation) {
    uint16_t instruction;
    if (operation == MUL) {
        instruction = 0xe3f7;
    }
    else if (operation == ADD) {
        instruction = 0xd801;
    }
    else if (operation == SUB) {
        instruction = 0xd829;
    }
    else if (operation == DIV) {
        *outBuffer++ = 0x48;
        *outBuffer++ = 0x31;
        *outBuffer++ = 0xd2;
        instruction = 0xf3f7;
    }
    memcpy(outBuffer, doOperation, sizeof(doOperation));
    outBuffer += 3;
    *(uint16_t*)outBuffer = instruction;
    outBuffer += sizeof(doOperation) - 3;
    return outBuffer;
}

int toPostfix(struct token* tokens, int length) { // no maxLength parameter. Postfix is never longer than infix.
    struct token t;
    int writeIndex = 0;
    int readIndex = 0;
    struct token operatorStack[128];
    int operatorStackIndex = 0;
    for (readIndex = 0; readIndex < length; readIndex++) {
        t = tokens[readIndex];
        switch (t.type) {
            case VALUE:
                tokens[writeIndex++] = t;
                break;
            case OPERATOR:
                while (operatorStackIndex >= 1 && compareOrder(t, operatorStack[operatorStackIndex-1]) >= EQUAL && operatorStack[operatorStackIndex-1].type != PARENTHESES_OPEN) {
                    tokens[writeIndex++] = operatorStack[operatorStackIndex-1];
                    operatorStackIndex--;
                }
                operatorStack[operatorStackIndex] = t;
                operatorStackIndex++;
                break;
            case PARENTHESES_OPEN:
                operatorStack[operatorStackIndex] = t;
                operatorStackIndex++;
                break;
            case PARENTHESES_CLOSE:
                while (operatorStack[operatorStackIndex-1].type != PARENTHESES_OPEN) {
                    tokens[writeIndex++] = operatorStack[operatorStackIndex-1];
                    operatorStackIndex--;
                }
                operatorStackIndex--;
                break;
            case NAME:
                tokens[writeIndex++] = t;
                break;
            default:
                printf("PANIC in toPostfix switch (t.type): default case should be unreachable. have %d as t.type\n", t.type);
        }
    }
    while (operatorStackIndex > 0) {
        tokens[writeIndex++] = operatorStack[operatorStackIndex-1];
        operatorStackIndex--;
    }
    return writeIndex;
}

int64_t calculate(struct token op1, struct token op2, struct token operator) {
    // Cannot return an error value with transparency on whether it is a valid return.
    // Would like to validate that the arguments have the right type tag, but cannot meaningfully return an error.
    // Could use an error-out param or a struct return, but for simplicity, just use the function correctly.
    switch (operator.value) {
        case ADD:
            return op1.value + op2.value;
        case SUB:
            return op1.value - op2.value;
        case MUL:
            return op1.value * op2.value;
        case DIV:
            return op1.value / op2.value; // This case should not trigger but will be included for thoroughness.
    }
}

int foldConstants(struct token* tokens, int length) {
    int readIndex = 1;
    int writeIndex = 0;
    struct token t;
    for (0; readIndex < length; readIndex++) {
        t = tokens[readIndex];
        if (t.type == OPERATOR && tokens[writeIndex].type == VALUE && tokens[writeIndex-1].type == VALUE) {
            writeIndex--;
            tokens[writeIndex].value = calculate(tokens[writeIndex], tokens[writeIndex+1], tokens[readIndex]);
        }
        else {
            writeIndex++;
            if (writeIndex != readIndex) {
                tokens[writeIndex] = tokens[readIndex];
            }
        }
    }
    return writeIndex+1;
}

void* makeFunction(char* string, uint8_t* outBuffer) {
    struct token tokens[128];
    int length = tokenizeString(string, tokens, 128);
    length = toPostfix(tokens, length);
    length = foldConstants(tokens, length);
    printf("Final postfix length: %d\n", length);
    if (length < 0) return NULL;
    uint8_t* ptr = outBuffer;

    struct token t;
    for (int i = 0; i < length; i++) {
        t = tokens[i];
        switch (t.type) {
            case VALUE:
                if ((int64_t) t.value != (int32_t) t.value) {
                    memcpy(ptr, instructions[MOVABSR1].bytes, instructions[MOVABSR1].length);
                    ptr += 2; // align with constant start
                    *(uint64_t*) ptr = t.value;
                    ptr += sizeof(uint64_t);
                    memcpy(ptr, instructions[PUSHR1].bytes, instructions[PUSHR1].length);
                    ptr += instructions[PUSHR1].length;
                }
                else {
                    memcpy(ptr, instructions[PUSHCONST].bytes, instructions[PUSHCONST].length);
                    ptr++; // align with constant start
                    *(uint32_t*) ptr = t.value;
                    ptr += sizeof(uint32_t);
                }
                break;
            case OPERATOR:
                ptr = copyOperation(ptr, t.value);
                break;
            case NAME:
                memcpy(ptr, instructions[PUSHARG].bytes, instructions[PUSHARG].length);
                ptr++;
                break;
            default:
                printf("PANIC in makeFunction switch (t.type): default case should be unreachable. have %d as t.type\n", t.type);
                return NULL;
        }
    }
    *ptr++ = 0x58; // pop rax
    *ptr++ = 0xc3; // ret
    return ptr;
}

int main() {
    char input[1024];
    strcpy(input, "x + 9 * 9 + 9 - 100 + 300 - 1 * 40");
    printf("Enter an expression in terms of x.\n");
    //fgets(input, sizeof(input), stdin);
    commonBuffer = mmap(NULL, getpagesize(), PROT_READ | PROT_EXEC | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    uint8_t* start = commonBuffer;
    f_x myfunc = (f_x) start;
    start = makeFunction(input, (uint8_t*) myfunc);
    for (int i = 0; i < 25; i++) {
        printf("f(%d) = %lu\n", i, myfunc(i));
    }
}
