import argparse
Block_Comment = False

def filter_Comments(input_file):
    global Block_Comment
    lines = []
    with open(input_file, 'r') as infile:
        for line in infile:
            if(Block_Comment):
                if(Block_Comment == True) and ("*/" in line):
                    line = line.split("*/", 1)[1]               
                    Block_Comment = False
                else:
                    line = ""

            if(Block_Comment == False) and ("/*" in line):
                tmp = line.split("/*", 1)
                if("*/" in tmp[1]):
                    line = tmp[0] + line.split("*/", 1)[1]
                    Block_Comment = False
                else:
                    line = tmp[0]
                    Block_Comment = True
                
            if(Block_Comment == False) and ("//" in line):
                line = line.split("//", 1)[0]

            lines.append(line)

    return lines
    

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="C program file")
    parser.add_argument('-i', '--input', required=True)
    parser.add_argument('-o', '--output', required=False, default="out.txt", help="Output file to save content without comments")

    args = parser.parse_args()
    print(args.input)
    print(args.output)
    lines = filter_Comments(args.input)
    with open(args.output, 'w') as outfile:
        i2cSetBus = -1
        i2cSetId = -1
        for line in lines:
            line = line.strip()
            if line == "":
                continue
            values = line.split(',')
            values = [item for item in values if item != ""]                  

            if (len(values) == 5):
                if(i2cSetBus != values[0]):
                    i2cSetBus = values[0]
                    outfile.write("SET-BUS " + i2cSetBus + "\n")

                if(i2cSetId != values[1]):
                    i2cSetId = values[1]
                    outfile.write("SET-ID " + hex((int(i2cSetId, 16) >> 1)) + "\n")

                addr = values[2] + values[3].split('0x')[1]

                outfile.write("WB-16 " + addr + " " + values[4] + "\n")

            # Assume Delay
            if (len(values) == 2):
                outfile.write("DELAY 1\n")

            
