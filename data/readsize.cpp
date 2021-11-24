#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

using namespace std;

int main(int argc, char** argv){
    if (argc != 3){
        throw runtime_error("invalid argument");
    }
    string filename(argv[1]);
    int readsize = atoi(argv[2]);
    
    FILE *fptr;
	fptr = fopen(filename.c_str(),"r");
	fseek(fptr, 0, SEEK_END);
	int filelen = (int)ftell(fptr);	
	fseek (fptr, 0, SEEK_SET);

    if (readsize>filelen)
        readsize = filelen;
    
    vector<char> buf;
    buf.resize(readsize);
    int ret = fread((void*)&(buf[0]), sizeof(char), readsize, fptr);

    if (ret != readsize)
        printf("SOMETHING WRONG WITH READ\n");
    
    FILE * newfile = fopen((string("clip_")+filename).c_str(), "w");
    ret = fwrite((void*)&(buf[0]), sizeof(char), readsize, newfile);

    if (ret != readsize)
        printf("SOMETHING WRONG WITH WRITE\n");

    fclose(fptr);
    fclose(newfile);

    return 0;
}