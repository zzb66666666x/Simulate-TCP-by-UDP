import sys
import os

# print('Number of arguments:', len(sys.argv), 'arguments.')
# print('Argument List:', str(sys.argv))

if (len(sys.argv) != 2):
    print("invalid argument")
else:
    filename = sys.argv[1]
    print(os.path.getsize(os.path.dirname(__file__)+"/"+filename))
