pi = 3.14159265358979323846264338279

combos = {}

bestCombo = None

bestValue = 0

for i in range(10000):
    for ii in range(10000):
        if ii != 0:
            if 3.1 <= i/ii <= 3.2:
                combos.update({(i, ii): i/ii})
            if abs(pi - (i/ii)) < abs(pi - bestValue):
                bestCombo = (i, ii)
                bestValue = i/ii

print(combos)
print(bestCombo)
print(bestValue)
