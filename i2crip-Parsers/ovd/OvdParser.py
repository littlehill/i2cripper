import argparse
import os
Block_Comment = False

def filter_Comments(input_file):
    global Block_Comment
    lines = []
    with open(input_file, 'r', errors='ignore') as infile:
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
    
def filter_functions(lines):
    length = len(lines)
    newLines = [] * length
    i = 0
    while i < length:
        if "function" in lines[i]:
            count = 0
            while i < length:
                if(lines[i].count('{') > 0):
                    break
                i += 1
                
            while i < length:
                count += lines[i].count('{') - lines[i].count('}')
                if(count == 0):
                    break
                i += 1
        else:
            newLines.append(lines[i])
        i += 1
        
    return newLines

def filter_misc_ovd(lines):
    length = len(lines)
    newLines = [] * length

    for line in lines:
        line = line.strip()

        # If empty
        if not line:
            continue
        
        lineLength = len(line)

        # Line is seporator
        if "@@" in line:
            line = "@@" + line.split("@@", 1)[1]

        # Line too small
        if(lineLength < 2):
            continue

        # Line is comment
        if line[:2] == ";;":
            continue
        
        newLines.append(line)

    return newLines

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Ovd file")
    parser.add_argument('-i', '--input', required=True)
    parser.add_argument('-o', '--output', required=False, default="out.txt", help="Output file to save content without comments")

    args = parser.parse_args()
    print(args.input)
    print(args.output)
    lines = filter_Comments(args.input)
    lines = filter_functions(lines)
    lines = filter_misc_ovd(lines)

    isFileOpen = False
    output = 0
    outputIndex = 0
    outputFolder = "./output/"

    if not os.path.exists(outputFolder):
        os.makedirs(outputFolder)
    # Remove files from directory
    file_list = os.listdir(outputFolder)
    for filename in file_list:
        file_path = os.path.join(outputFolder, filename)
        if os.path.isfile(file_path):
            try:
                os.remove(file_path)
            except Exception as e:
                print(f"Error removing file: {file_path}, {e}")

    filename = ""
    i2cSalveAddress = "NONE"

    for line in lines:
        if "@@" in line:
            #TODO split into new file
            if(isFileOpen == True):
                output.close()
            isFileOpen = False
            filename = outputFolder + line.split("@@")[1].strip() + "_" + str(outputIndex) + ".txt"
            try:
                output = open(filename, 'w')
                isFileOpen = True
                outputIndex += 1
            except Exception as e:
                print("Failed to open file: " + filename + "\n")
                print("Exiting...\n")
                exit()

            output.write("SET-BUS PUT_BUS_NUMBER_HERE\n")
            i2cSalveAddress = "NONE"
        if(isFileOpen == False):
            continue

        # Empty
        if not line:
            continue

        # If its a delay command
        if "; Delay " in line:
            line = line.split("; Delay ")[1]
            if "ms" not in line:
               continue

            line = "DELAY " + line.split("ms")[0]
               
        else:
            splt = line.split(" ")
            # not valid command
            if(len(splt) != 3):
                continue
            try:
                # Test to see if there are actaully numbers
                values = [int(splt[0], 16), int(splt[1], 16), int(splt[2], 16)]
            except ValueError:
                continue
            
            isComment = False
            if(values[0] >= 0x7F):
                line = "//" + line
                isComment = True
            elif(len(splt[2]) == 2):
                line = "WB-16 0x" + splt[1] + " 0x" + splt[2]
            elif(len(splt[2]) <= 4):
                line = "WW-16 0x" + splt[1] + " 0x" + splt[2]
            else:
                line = "//" + line
                isComment = True

            if ((i2cSalveAddress != splt[0]) and (not isComment)):
                output.write("SET-ID 0x" + splt[0] + "\n")
                i2cSalveAddress = splt[0]

        output.write(line + "\n")

    if(isFileOpen):
        output.close()
            
    print("FINISHED")
