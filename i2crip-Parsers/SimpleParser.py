import argparse
Block_Comment = False


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="C program file")
    parser.add_argument('-i', '--input', required=True)
    parser.add_argument('-o', '--output', required=False, default="out.txt", help="Output file to save content without comments")

    args = parser.parse_args()
    print(args.input)
    print(args.output)

    file = open(args.input)
    lines = file.readlines()
    file.close()
    with open(args.output, 'w') as outfile:
        outfile.write("SET-BUS 16\n")
        outfile.write("SET-ID 0x10\n")
        for line in lines:
            line = line.strip()
            if line == "":
                continue
            values = line.split(' ')
            values = [item for item in values if item != ""]                  
            if (len(values) == 2):
                outfile.write("WB-16 " + values[0] + " " + values[1] + "\n")
            
