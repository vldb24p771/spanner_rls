def extract_numbers(lines):
    array1 = []
    array2 = []
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.isdigit() and 1 <= int(line) <= 80:
            i += 23
            if i + 1 < len(lines):
                next_line = lines[i].strip()
                if "/" in next_line:
                    number_str = next_line.split("/")[-1].strip()
                    try:
                        number = float(number_str)
                        array1.append(number)
                    except ValueError:
                        pass
                next_line = lines[i + 3].strip()
                if "/" in next_line:
                    number_str = next_line.split("/")[-1].strip()
                    try:
                        number = float(number_str)
                        array2.append(number)
                    except ValueError:
                        pass
            i += 1
        else:
            i += 1
    return array1, array2

# 从文件中读取文本
input_file_path = "tem_result_waitdie.txt"  # 输入文件路径
with open(input_file_path, "r") as file:
    text = file.read()

# 按行拆分文本
lines = text.strip().split('\n')

# 提取数字
numbers_array1, numbers_array2 = extract_numbers(lines)

final_array1 = [0] * 16
final_array2 = [0] * 16

j = 0
for i in range(0, len(numbers_array1)):
    if i % 16 == 0:
        j+=1
    final_array1[i % 16] += numbers_array1[i]
    final_array2[i % 16] += numbers_array2[i]

for i in range(0, 16):
    final_array1[i] /= j
    final_array2[i] /= j
    final_array1[i] = round(final_array1[i], 4)
    final_array2[i] = round(final_array2[i], 4)

    
# 存储数字到文件
output_file_path = "spanner_lat_tput_waitdie.txt"  # 输出文件路径
with open(output_file_path, "w") as file:
    file.write(str(len(final_array1)) +"\n")
    for i in range(0, len(final_array1)):
        file.write(str(final_array1[i]) + ",")
    file.write("\n")
    for i in range(0, len(final_array2)):
        file.write(str(final_array2[i]) + ",")

print("Numbers saved to", output_file_path)