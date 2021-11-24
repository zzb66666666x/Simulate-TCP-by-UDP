#include <iostream>
#include <stdio.h>

using namespace std;

int main(int argc, char** argv){
    if (argc != 2){
        throw runtime_error("invalid argument");
    }
    string filename(argv[1]);
    
    FILE *fptr;
	fptr = fopen(filename.c_str(),"r");

    fseek(fptr, 0, SEEK_END);
    int size = (int)ftell(fptr);
    fseek(fptr, 0, SEEK_SET);

    fclose(fptr);

    cout<< "file size: "<<size<<endl;

    return 0;
}