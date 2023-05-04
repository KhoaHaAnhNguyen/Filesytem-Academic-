// The MIT License (MIT)
// 
// Copyright (c) 2016 Trevor Bakker 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>

#define BLOCK_SIZE 1024 // The filesystem block size shall be 1024 bytes.
#define NUM_BLOCKS 65536 // The filesystem shall have 65536 blocks
#define BLOCKS_PER_FILE 1024 // define number of blocks per file
#define NUM_FILES 256 // The filesystem shall support up to 256 files.
#define FIRST_DATA_BLOCK 1001
#define MAX_FILE_SIZE 1048576
#define MAX_NAME_SIZE 30
#define HIDDEN 0x1
#define READONLY 0x2
uint8_t data [NUM_BLOCKS][BLOCK_SIZE]; //declare data
uint8_t * free_blocks;
uint8_t * free_inodes;

// define entry structure
struct _directoryEntry {
  char filename[64];
  short in_use;
  int32_t inode;
}; struct _directoryEntry * directory;

// define inode structure
struct inode{
  int32_t blocks[BLOCKS_PER_FILE];
  short in_use;
  uint8_t attribute;
  uint32_t file_size;
}; struct inode * inodes;

FILE *file;
char image_name[64];
uint8_t image_open;
int show_hidden = 0;
int show_attributes = 0;


#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // only supports four arguments

int32_t findFreeBlock(){
  for (int i = 0; i < NUM_BLOCKS; i++){
    if (free_blocks[i]){
      return i + FIRST_DATA_BLOCK;
    }
  }
  return -1;
}

int32_t findFreeInode(){
  for (int i = 0; i < NUM_FILES; i++){
    if (free_inodes[i]){
      return i;
    }
  }
  return -1;
}

int32_t findFreeInodeBlock(int32_t inode){
  for (int i = 0; i < BLOCKS_PER_FILE; i++){
    if (inodes[inode].blocks[i] == -1){
      return i;
    }
  }
  return -1;
}

void initialization () {
  directory = (struct _directoryEntry*)&data[0][0];
  inodes = (struct inode *)&data[20][0];
  free_blocks = (uint8_t *)&data[1000][0];
  free_inodes = (uint8_t *)&data[19][0];
  memset(image_name,0,64);
  image_open = 0;
  for (int i = 0; i < NUM_FILES; i++){
    directory[i].in_use = 0;
    directory[i].inode = -1;
    free_inodes[i] = 1;
    memset(directory[i].filename, 0, 64);
    for (int j = 0; j < NUM_BLOCKS; j++){
      inodes[i].blocks[j] = -1;
      inodes[i].in_use = 0;
      inodes[i].attribute = 0;
      inodes[i].file_size = 0;
    }
  }
  for (int j = 0; j < NUM_BLOCKS; j++){
    free_blocks[j] = 1;
  }  
}
// Retrieve a file from the file system.
void retrievefs(char * filename, char * newfilename){
  // Get the file metadata using the stat function
  struct stat buf;
  int ret = stat (filename, &buf);
  if (ret == -1) {
    perror("File does not exist\n");
    return;
  }
  
  // Search for the file in the file system directory
  int32_t inode_index = -1;
  for (int i = 0; i < NUM_FILES; i++) {
    // If the file name matches and it's in use, get its inode index
    if (strcmp(directory[i].filename, filename) == 0 && directory[i].in_use) {
      inode_index = directory[i].inode;
      break;
    }
  }
  // If the file was not found, print an error message and return
  if (inode_index == -1) {
    printf("Error: File not found.\n");
    return;
  }

  // Create a new filename if one is not provided
  if (newfilename == NULL) {
    newfilename = filename;
  }

  // Open the output file for writing
  FILE * ofp = fopen(newfilename, "w");
  if (ofp == NULL) {
    printf("Error: Could not open output file: %s\n", newfilename);
    perror("Opening output file returned");
    return;
  }

  // Copy the file data to the output file using code from block_copy.c
  int32_t copy_size   = buf . st_size;
  int32_t block_index = 0;
  int32_t offset = 0;
  printf("Writing %d bytes to %s\n", (int) buf . st_size, newfilename);
  while (copy_size > 0) {
    int32_t num_bytes;
    // If the remaining data is less than a full block, only copy that much
    if (copy_size < BLOCK_SIZE) {
      num_bytes = copy_size;
    } else {
      num_bytes = BLOCK_SIZE;
    }
    // Write the data to the output file and update the variables for the next block
    fwrite(data[block_index], num_bytes, 1, ofp ); 
    copy_size -= BLOCK_SIZE;
    offset += BLOCK_SIZE;
    block_index++;
    fseek(ofp, offset, SEEK_SET);
  }

  // Close the output file and print a success message
  fclose(ofp);
}

// Print <number of bytes> bytes from the file, in hexadecimal, starting at <starting byte>
void readfs(char * filename, int starting, int num_bytes) {
  int32_t inode_index = -1;
  int32_t current = 0;
  int32_t offset = 0;
  
  // Find the inode index of the file
  for (int i = 0; i < NUM_FILES; i++){
    if(strcmp(directory[i].filename, filename) == 0){
    inode_index = directory[i].inode;
    break;
    }
  }
  if(inode_index == -1){
    perror("Failed to find free inode\n");
    return;
  }
  
  // Read data block by block
  uint8_t* file_data = (uint8_t*) malloc(num_bytes);
  for (int i = 0; i< NUM_BLOCKS; i++){
    // Exit the loop if we've read all the blocks
    if (current == BLOCKS_PER_FILE){
      break;
    }
    // Exit the loop if we've reached an invalid block
    if (inodes[inode_index].blocks[current] == -1){
      break;
    }
    // Copy the block's data into the file_data buffer
    memcpy(file_data + offset, data[inodes[inode_index].blocks[current]], BLOCK_SIZE);
    current++;
    offset += BLOCK_SIZE;
  }
  
  // Print the file data starting at the specified byte offset
  for (int i = starting; i < starting + num_bytes; i++) {
    printf("%02x ", file_data[i]);
  }
  printf("\n");
  
  // Free the memory allocated for the file data buffer
  free(file_data);
}

void deletefs (char * filename) {
  int file_found = 0;
  int32_t inode_index = -1;
  int32_t block_index = -1;

  // finding inode of that filename
  for (int i =- 0; i < NUM_FILES; i++){
    if(directory[i].in_use && strcmp(directory[i].filename, filename) == 0){
      inode_index = directory[i].inode;
      directory[i].in_use = 0;
      directory[i].inode = -1;
      memset(directory[i].filename,0,64);
      file_found = 1;
      break;
    }
  }
  if(!file_found){
    printf("Error: file not found\n");
    return;
  }
  // free all blocks used by file
  for(int i = 0; i < BLOCKS_PER_FILE; i++){
    block_index = inodes[inode_index].blocks[i];
    if (block_index != -1){
      free_blocks[block_index - FIRST_DATA_BLOCK] = 1;
      inodes[inode_index].blocks[i] = -1;
    }
  }
  // free that inode then update the variables
  free_inodes[inode_index] = 1;
  inodes[inode_index].in_use = 0;
  inodes[inode_index].attribute = 0;
  inodes[inode_index].file_size = 0;
  printf("%s deleted!", filename);
}

void undelfs (char * filename) {
  int32_t inode_index = -1;
  // searching for the inode index of the file in the directory
  for (int i = 0; i < NUM_FILES; i++){
    if(directory[i].in_use && strcmp(directory[i].filename, filename) == 0){
      inode_index = directory[i].inode;
      // marking the inode and directory entry as in use
      directory[i].in_use = 1;
      inodes[inode_index].in_use = 1;
      printf("%s undeleted", filename);
      return;
    }  
  }
  // if file not found
  printf("Can not find the file.\n");
}

void listfs () {
  int not_found = 1;
  for (int i = 0; i < NUM_FILES; i++){
    // todo hidden files
    // check if the file is hidden and whether to show it
    // if yes, list it also
    if (!show_hidden && directory[i].filename[0] == '.') {
      printf("%s\n", directory[i].filename);
    }
    // if -a parameter is provided, list the attribute as well
    if (show_attributes) {
      printf("%s - %d\n", directory[i].filename, inodes[i].attribute);
    }  
    //For each file, if it is in use, the function stores its name in a temporary buffer "filename".  
    if (directory[i].in_use) {
      not_found = 0;
      char filename[64];
      memset (filename,0,65);
      strncpy(filename,directory[i].filename,strlen(directory[i].filename));
    }
  }
  if (not_found){
    perror("No files found");
    return;
  }
}

int dffs() {
  int count = 0;
  // Count the total amount of free space in the filesystem 
  //by counting the number of free blocks and multiplying it by the block size.
  for (int i = 0; i < NUM_BLOCKS; i++){
    if (free_blocks[i]){
      count++;
    }
  }
  return count * BLOCK_SIZE;  
}

void insertfs (char * filename){
  //check for NULL in filename
  if (filename == NULL){
    perror("Filename is NULL\n");
    return;
  }
  //check if the file is exists
  struct stat buf;
  int ret = stat (filename, &buf);
  if (ret == -1) {
    perror("File is not exist\n");
    return;
  }
  //check if the file name too long
  if (strlen(filename) > MAX_NAME_SIZE) {
    perror("File name too long\n");
    return;
  }
  //check if the file is too big
  if(buf.st_size > MAX_FILE_SIZE){
    perror("File is too big");
    return;
  }
  //check if there is enough disk space
  if(buf.st_size > dffs()){
    perror("Not enough disk space");
    return;
  }
  // find empty directory entry
  else{
    int directory_entry = -1;
    for (int i = 0; i < NUM_FILES; i ++){
      if(directory[i].in_use == 0){
        directory_entry = i;
        break;  
      }
    }
    if (directory_entry == -1) {
      perror("Failed to find a free directory entry\n");
      return;
    }
    FILE *ifp = fopen (filename, "r" ); 
    printf("Reading %d bytes from %s\n", (int) buf . st_size, filename);
    // Save off the size of the input file since we'll use it in a couple of places and 
    // also initialize our index variables to zero. 
    int copy_size   = buf . st_size;
    // We want to copy and write in chunks of BLOCK_SIZE. So to do this 
    // we are going to use fseek to move along our file stream in chunks of BLOCK_SIZE.
    // We will copy bytes, increment our file pointer by BLOCK_SIZE and repeat.
    int offset      = 0;     
    // We are going to copy and store our file in BLOCK_SIZE chunks instead of one big 
    // memory pool. Why? We are simulating the way the file system stores file data in
    // blocks of space on the disk. block_index will keep us pointing to the area of
    // the area that we will read from or write to.          
    int block_index = -1;
    //find a free inode
    int32_t inode_index = findFreeInode();
    if(inode_index == -1){
      perror("Failed to find free inode\n");
      return;
    }
    //place the file in the directory
    directory[directory_entry].in_use = 1;
    directory[directory_entry].inode = inode_index;
    strncpy(directory[directory_entry].filename,filename, strlen(filename));
    inodes[inode_index].file_size = buf.st_size;
    // copy_size is initialized to the size of the input file so each loop iteration we
    // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
    // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
    // we have copied all the data from the input file.
    while( copy_size > 0 ){
      // Index into the input file by offset number of bytes.  Initially offset is set to
      // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
      // then increase the offset by BLOCK_SIZE and continue the process.  This will
      // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, 3*BLOCK_SIZE, etc.
      fseek( ifp, offset, SEEK_SET );
      //find a free_blocks
      block_index = findFreeBlock();
      if (block_index == -1){
        perror("Failed to find free block\n");
        return;
      }
      // Read BLOCK_SIZE number of bytes from the input file and store them in our
      // data array. 
      int32_t bytes  = fread(data[block_index], BLOCK_SIZE, 1, ifp );
      // save the block in the inode
      int32_t inode_block = findFreeInodeBlock(inode_index);
      inodes[inode_index].blocks[inode_block] = block_index;
      // If bytes == 0 and we haven't reached the end of the file then something is 
      // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
      // It means we've reached the end of our input file.
      if( bytes == 0 && !feof( ifp ) ){
        perror("An error occured reading from the input file.\n");
        return;
      }
      // Clear the EOF file flag.
      clearerr( ifp );
      // Reduce copy_size by the BLOCK_SIZE bytes.
      copy_size -= BLOCK_SIZE;
      // Increase the offset into our input file by BLOCK_SIZE.  This will allow
      // the fseek at the top of the loop to position us to the correct spot.
      offset    += BLOCK_SIZE;
      // Increment the index into the block array 
      // DO NOT just increment block index in your file system
      block_index = findFreeBlock();
    }
    // We are done copying from the input file so close it out.
    fclose( ifp );
  }
}


void openfs(char * filename){
  // reads the contents of the file into system blocks
  file = fopen(filename, "r");
  strncpy(image_name,filename, strlen(filename));
  fread(&data[0][0],BLOCK_SIZE,NUM_BLOCKS,file);
  // set to 1 to indicate that the filesystem image has been opened
  image_open = 1;
  fclose(file);
}

void closefs() {
  if (image_open == 0){
    perror("Disk image is not open\n");
    return; 
  }
  fclose(file);
  // set to 0 to indicate that the filesystem image has been opened
  // 0 initialize image_name to 
  image_open = 0;
  memset(image_name,0,64);  
}

// create new file system on disk
// initialize superblock then allocate inodes and data blocks
// after that, create the root directory and set its metadata
// finally, write the file system to disk
void createfs (char * filename){
  file = fopen(filename, "w");
  strncpy(image_name,filename, strlen(filename));
  memset(data,0,NUM_BLOCKS * BLOCK_SIZE);
  image_open = 1;
  for (int i = 0; i < NUM_FILES; i++){
    directory[i].in_use = 0;
    directory[i].inode = -1;
    free_inodes[i] = 1;
    memset(directory[i].filename, 0, 64);
    for (int j = 0; j < NUM_BLOCKS; j++){
      inodes[i].blocks[j] = -1;
      inodes[i].in_use = 0;
      inodes[i].attribute = 0;
      inodes[i].file_size = 0;
    }
  }  
  for (int j = 0; j < NUM_BLOCKS; j++){
    free_blocks[j] = 1;
  } 
  fclose(file);
}

void savefs (){
  if (image_open == 0){
    perror("Disk image is not open\n"); 
  }
  file = fopen(image_name, "w");
  //Save the current state of the filesystem by writing its data to a file.
  fwrite( &data[0][0], BLOCK_SIZE, NUM_BLOCKS, file);
  memset(image_name,0,64);
  fclose(file);
}

void attribfs (char * filename, int attri, int set) {
  int file_found = 0;
  //Searches for the file in the directory and updates the corresponding inode's attribute.
  for (int i = 0; i < NUM_FILES; i++){
    int32_t inode_index = directory[i].inode;
    if (directory[i].in_use && strcmp(directory[i].filename, filename) == 0) {
      file_found = 1;
      // if attri  = 1 (hidden) and set = 1 (set) -> set hidden 
      if (set == 1){
        if (attri == 1){
          inodes[inode_index].attribute |= HIDDEN;
        } 
        // if attri  = 2 (readonly) and set = 1 (set) -> set readonly
        if (attri == 2){
          inodes[inode_index].attribute |= READONLY;
        }
      }
      else{ //otherwise set = 0 (remove)
        // if in hidden category and it is not hidden  -> remove    
        if ((attri == 1) &&  ( inodes[i].attribute & HIDDEN ) ){
          inodes[inode_index].attribute &= ~(HIDDEN);
        }
        // if in readonly category and it is not readonly  -> remove  
        if ((attri == 2) && ( inodes[i].attribute & READONLY )){
          inodes[inode_index].attribute &= ~(READONLY);
          }          
        }      
      }
      break;
    }
  if (!file_found) {
    printf("File not found\n");
  }  
}

void encryptfs(char *filename, uint8_t cipher) {
  // Open the file in read-write binary mode
  FILE *fp = fopen(filename, "r+b");
  if (fp == NULL) {
    printf("Error: File '%s' not found!\n", filename);
    return;
  }
  // Get the file size and check if it exceeds the maximum file size
  fseek(fp, 0L, SEEK_END);
  int32_t file_size = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  if (file_size > MAX_FILE_SIZE) {
    printf("Error: File '%s' exceeds maximum size of %d bytes!\n", filename, MAX_FILE_SIZE);
    return;
  }
  // Calculate the number of blocks needed to store the file
  int32_t blocks_needed = file_size / BLOCK_SIZE + 1;

  int32_t inode_index = -1;
  int32_t block_index = -1;
  int32_t inode_block_index = -1;
  // Find or allocate an inode for the file
  for (int i = 0; i < NUM_FILES; i++) {
    if (directory[i].in_use && strcmp(directory[i].filename, filename) == 0) {
      inode_index = directory[i].inode;
      break;
    }
  }

  if (inode_index == -1) {
    inode_index = findFreeInode();
    if (inode_index == -1) {
      printf("Error: Could not find free inode to store file '%s'!\n", filename);
      return;
    }
    // then update the directory entry to active
    directory[inode_index].in_use = 1;
    directory[inode_index].inode = inode_index;
    strcpy(directory[inode_index].filename, filename);
    free_inodes[inode_index] = 0;
  }
  // Allocate blocks for the file 
  for (int i = 0; i < blocks_needed; i++) {
    block_index = findFreeBlock();
    if (block_index == -1) {
      printf("Error: Could not find free block to store file '%s'!\n", filename);
      return;
    }
    if (i == 0) {
      inodes[inode_index].blocks[0] = block_index;
      inodes[inode_index].in_use = 1;
      inodes[inode_index].attribute = 0;
      inodes[inode_index].file_size = file_size;
    }
    else {
      inode_block_index = findFreeInodeBlock(inode_index);
      inodes[inode_index].blocks[inode_block_index] = block_index;
    }
    free_blocks[block_index - FIRST_DATA_BLOCK] = 0;
    // Read a block of data from the file and encrypt it
    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    fread(buf, 1, BLOCK_SIZE, fp);
    for (int j = 0; j < BLOCK_SIZE; j++) {
      buf[j] ^= cipher;
    }
    // Write the encrypted data back to the file
    fseek(fp, i * BLOCK_SIZE, SEEK_SET);
    fwrite(buf, 1, BLOCK_SIZE, fp);
  }

  fclose(fp);
  printf("File '%s' encrypted successfully!\n", filename);
}
// same as encrypts
// In XOR cipher, a bitwise XOR operation is performed on the
// plaintext with a key to generate the ciphertext.
//To decrypt the ciphertext, the same key is used to perform the XOR operation again
void decryptfs(char *filename, uint8_t cipher) {
  FILE *fp = fopen(filename, "r+b");
  if (fp == NULL) {
    printf("Error: File '%s' not found!\n", filename);
    return;
  }
  
  fseek(fp, 0L, SEEK_END);
  int32_t file_size = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  if (file_size > MAX_FILE_SIZE) {
    printf("Error: File '%s' exceeds maximum size of %d bytes!\n", filename, MAX_FILE_SIZE);
    return;
  }

  int32_t blocks_needed = file_size / BLOCK_SIZE + 1;

  int32_t inode_index = -1;
  int32_t block_index = -1;
  int32_t inode_block_index = -1;

  for (int i = 0; i < NUM_FILES; i++) {
    if (directory[i].in_use && strcmp(directory[i].filename, filename) == 0) {
      inode_index = directory[i].inode;
      break;
    }
  }

  if (inode_index == -1) {
    inode_index = findFreeInode();
    if (inode_index == -1) {
      printf("Error: Could not find free inode to store file '%s'!\n", filename);
      return;
    }
    directory[inode_index].in_use = 1;
    directory[inode_index].inode = inode_index;
    strcpy(directory[inode_index].filename, filename);
    free_inodes[inode_index] = 0;
  }

  for (int i = 0; i < blocks_needed; i++) {
    block_index = findFreeBlock();
    if (block_index == -1) {
      printf("Error: Could not find free block to store file '%s'!\n", filename);
      return;
    }
    if (i == 0) {
      inodes[inode_index].blocks[0] = block_index;
      inodes[inode_index].in_use = 1;
      inodes[inode_index].attribute = 0;
      inodes[inode_index].file_size = file_size;
    }
    else {
      inode_block_index = findFreeInodeBlock(inode_index);
      inodes[inode_index].blocks[inode_block_index] = block_index;
    }
    free_blocks[block_index - FIRST_DATA_BLOCK] = 0;

    uint8_t buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    fread(buf, 1, BLOCK_SIZE, fp);
    for (int j = 0; j < BLOCK_SIZE; j++) {
      buf[j] ^= cipher;
    }
    fseek(fp, i * BLOCK_SIZE, SEEK_SET);
    fwrite(buf, 1, BLOCK_SIZE, fp);
  }

  fclose(fp);
  printf("File '%s' decrypted successfully!\n", filename);
}



int main(){

  char * command_string = (char*) malloc( MAX_COMMAND_SIZE );
  file = NULL;
  initialization();
    
  // reuse code from mav shell assignment
  while( 1 ){
  // Print out the msh prompt
    printf ("mfs> ");
    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (command_string, MAX_COMMAND_SIZE, stdin) );
    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];
    for( int i = 0; i < MAX_NUM_ARGUMENTS; i++ ){
      token[i] = NULL;
    }
    int   token_count = 0;                                                                                        
    // Pointer to point to the token
    // parsed by strsep
    char *argument_ptr = NULL;                                                                                                
    char *working_string  = strdup( command_string );                
    // we are going to move the working_string pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *head_ptr = working_string;
    // Tokenize the input strings with whitespace used as the delimiter
    while ( ( (argument_ptr = strsep(&working_string, WHITESPACE ) ) != NULL) && 
              (token_count<MAX_NUM_ARGUMENTS)){
      token[token_count] = strndup( argument_ptr, MAX_COMMAND_SIZE );
      if( strlen( token[token_count] ) == 0 ){
        token[token_count] = NULL;
      }
      token_count++;
    }

    // Command 
    
    // promting 'msh' then user does not input anything
    if (token[0] == NULL){
      continue;
    }

    else if ((strcmp("insert", token[0]) == 0 )){
      if (!image_open) {
        perror("Disk image is not opened\n");
        continue;
      }
      if (token[1] == NULL) {
        perror("No filename specified\n");
        continue;
      }
      insertfs(token[1]);
    }

    else if ((strcmp("retrieve", token[0]) == 0 )){
      if (!image_open) {
        perror("Disk image is not opened\n");
        continue;
      }
      else {
        if (token_count == 2) {
          retrievefs(token[1], NULL);
        } 
        else {
          retrievefs(token[1], token[2]);
        }
      }  
    }

    else if ((strcmp("read", token[0]) == 0 )){
      if (!image_open) {
        perror("Disk image is not opened\n");
        continue;
      }
      readfs(token[1],atoi(token[2]),atoi(token[3]));
    }

    else if ((strcmp("delete", token[0]) == 0 )){
      if (token_count < 2) {
        printf("Error: missing filename argument\n");
        continue;
      }
      if (!image_open) {
        perror("Disk image is not opened\n");
        continue;
      }
      deletefs(token[1]);
    }

    else if ((strcmp("undelete", token[0]) == 0 )){
      if (!image_open) {
        perror("Disk image is not opened\n");
        continue;
      }
      if (token_count < 2) {
        printf("Error: missing filename argument\n");
        continue;
      }
      undelfs(token[1]);
    }
    
    else if ((strcmp("list", token[0]) == 0 )){
      if (!image_open) {
        perror("Disk image is not opened\n");
        continue;
      }
      for (int i = 1; i < token_count; i++) {
        if (strcmp(token[i], "-h") == 0) {
          show_hidden = 1;
        } else if (strcmp(token[i], "-a") == 0) {
          show_attributes = 1;
        }
      }
      listfs();
      show_hidden = 0;
      show_attributes = 0;
    }

    else if ((strcmp("df", token[0]) == 0 )){
      if (!image_open){
        perror("Disk image is not opened\n");
        continue;
      }
      printf("%d bytes free\n", dffs());
    }

    else if ((strcmp("open", token[0]) == 0 )){
      if (token[1] == NULL) {
        perror("No filename specified\n");
        continue;
      }
      openfs(token[1]);
    }

    else if ((strcmp("close", token[0]) == 0 )){
      closefs();
    }

    else if ((strcmp("createfs", token[0]) == 0 )){
      if (token[1] == NULL) {
        perror("No filename specified\n");
        continue;
      }
      createfs(token[1]);
    }

    else if ((strcmp("savefs", token[0]) == 0 )){
      savefs();
    }
    
    else if ((strcmp("attrib", token[0]) == 0 )){
      // att = 1 -> h attribute
      // att = 2 -> r attribute
      // set = 1 -> +
      // set = 0 -> -
      int set = 1;
      int att = 0;
      if (strcmp(token[1], "+h") == 0) {
        att = 1;
        set = 1;
        attribfs(token[2], att,set);
      }
      else if (strcmp(token[1], "-h") == 0) {
        att = 1;
        set = 0;
        attribfs(token[2], att,set);
      }
      else if (strcmp(token[1], "+r") == 0) {
        att = 2;
        set = 1;
        attribfs(token[2], att,set);
      }
      else if (strcmp(token[1], "-r") == 0) {
        att = 2;
        set = 0;
        attribfs(token[2], att,set);
      }  
    }

    else if ((strcmp("encrypt", token[0]) == 0 )){
      encryptfs(token[1], atoi(token[2]));
    } 
    

    else if ((strcmp("decrypt", token[0]) == 0 )){
      decryptfs(token[1], atoi(token[2]));
    } 

    // compare the current command line with 'quit', if equals, exit with zero status
    else if ((strcmp("quit", token[0]) == 0)){
      exit(0);
    }

    else {
      printf("Invalid command! Try Again!\n");
      continue;
    }

    //___________________________________________________________________________________________________________________________//  

    // Cleanup allocated memory
    for( int i = 0; i < MAX_NUM_ARGUMENTS; i++ ){
      if( token[i] != NULL ){
        free( token[i] );
      }
    }
    free( head_ptr );
  }
  free( command_string );
  return 0;
  // e2520ca2-76f3-90d6-0242ac120003
}