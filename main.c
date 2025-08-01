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
    int value;
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
    {{0xc3}, 1}                    // ret
};

uint8_t doOperation[] = {
    0x5b,
    0x58,
    0x48, 0x00, 0x0, // The 0 bytes can be replaced by the appropriate operation.
    0x50
};

typedef uint64_t (*f_x)(int);

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
    while (*input != '\0') {
        while (isspace(*input)) 
            input++;
        if (*input == '(') {
            if (index == maxTokenLength) return -1;
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
            tokens[index++] = (struct token) {NAME, 0};
            input += length;
        }
        else if (isOperator(input, &length)) {
            if (index == maxTokenLength) return -1;
            int operator = *input;
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
            tokens[index++] = (struct token) {VALUE, atoi(input)};
            input += length;
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
    memcpy(outBuffer, doOperation, sizeof(doOperation));
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
        instruction = 0xf3f7;
    }
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
                printf("PANIC in makeFunction switch (t.type): default case should be unreachable. have %d as t.type\n", t.type);
        }
    }
    while (operatorStackIndex > 0) {
        tokens[writeIndex++] = operatorStack[operatorStackIndex-1];
        operatorStackIndex--;
    }
    return writeIndex;
}

void* makeFunction(char* string, uint8_t* outBuffer) {
    struct token tokens[128];
    int length = tokenizeString(string, tokens, 128);
    length = toPostfix(tokens, length);
    uint8_t* ptr = outBuffer;

    struct token t;
    for (int i = 0; i < length; i++) {
        t = tokens[i];
        switch (t.type) {
            case VALUE:
                memcpy(ptr, instructions[PUSHCONST].bytes, instructions[PUSHCONST].length);
                ptr++;
                *(uint32_t*) ptr = t.value;
                ptr += sizeof(uint32_t);
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
        }
    }
    *ptr++ = 0x58; // pop rax
    *ptr++ = 0xc3; // ret
    return ptr;
}

int main() {
    commonBuffer = mmap(NULL, getpagesize(), PROT_READ | PROT_EXEC | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    uint8_t* start = commonBuffer;
    f_x myfunc = (f_x) start;
    start = makeFunction("10 + 2", (uint8_t*) myfunc);
    printf("%lu\n", myfunc(1));
}
