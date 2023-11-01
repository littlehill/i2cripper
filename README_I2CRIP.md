# i2cripper
Addition to i2c-tools family. Loads scripted i2c operations and writes them in one go. Intended for testing init of more complex ICs like GMSL Ser/Des and e.g. image sensors.

For an example refer to i2cRupExample.txt

Usage: i2crip [ACTION] FILELOCATION
  ACTION is a flag to indicate read, write, or verify.
    -y (Yes))
    -s (Simulate)
    -q (Quiet)
    -h (Help)
    -v (Version)
  FILELOCATION is the path to the intput file
I2cTool Commands:
  SET-BUS <bus_number>: Set the I2C bus to the specified bus number.
  SET-ID <device_address>: Set the I2C device ID to the specified address.
  SUPPRESS-ERRORS [1|0]: Enable (1) to suppress errors, or (0) to enable error detection.
  LOG-FILE [1|0]: Enable (1) to log data to 'i2cRip.log', or (0) to disable data logging (default location).
  LOG-TERM [1|0]: Enable (1) to log data to the terminal, or (0) to disable terminal logging.
  DELAY <milliseconds>: Create a specified duration delay in milliseconds.
  RB-8 <register_address>: Read 1 byte from the 8-bit address.
  RB-16 <register_address>: Read 1 byte from the 16-bit address.
  RW-8 <register_address>: Read 1 word (2 bytes) from the 8-bit address.
  RW-16 <register_address>: Read 1 word (2 bytes) from the 16-bit address.
  WB-8 <register_address> <data>: Write 1 byte to the 8-bit address.
  WB-16 <register_address> <data>: Write 1 byte to the 16-bit address.
  WW-8 <register_address> <data>: Write 1 word (2 bytes) to the 8-bit address.
  WW-16 <register_address> <data>: Write 1 word (2 bytes) to the 16-bit address.
  VB-8 <register_address> <expected_data>: Read 1 byte and compare it to the expected data.
  VB-16 <register_address> <expected_data>: Read 1 byte and compare it to the expected data.
  VW-8 <register_address> <expected_data>: Read 2 bytes and compare them to the expected data.
  VW-16 <register_address> <expected_data>: Read 2 bytes and compare them to the expected data.
  Use '0x' prefix for hexadecimal numbers throughout the script.
  You can add comments using '//' within the command list.