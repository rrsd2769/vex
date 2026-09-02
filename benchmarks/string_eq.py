count = 0
i = 0
while i < 2000000:
    a = "benchmark"
    b = "benchmark"
    c = "other"
    if a == b:
        count = count + 1
    if a == c:
        count = count + 1
    i = i + 1
print(count)
