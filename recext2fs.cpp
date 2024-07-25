#include <sys/types.h>
#include <fcntl.h>
#include "ext2fs.h"
#include "ext2fs_print.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include "identifier.h"
#include <bitset>

using namespace std;

uint8_t *FS_BASE_PTR;
uint32_t INODE_SIZE;
uint32_t BLOCK_SIZE;
uint32_t NUM_OF_BLOCK_GROUPS;
struct ext2_super_block *SUPER_BLOCK;
uint8_t *DATA_IDENTIFIER;
uint32_t IDENTIFIER_SIZE;
uint32_t INODE_TABLE_BLOCK_NUM;

void *mmap_file_system(char *file_path)
{
    void *ptr = NULL;
    struct stat file_states;
    int file_descriptor;
    file_descriptor = open(file_path, O_RDWR);
    fstat(file_descriptor, &file_states);
    ptr = mmap(NULL, file_states.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    close(file_descriptor);
    return ptr;
}

uint8_t *get_block_pointer(uint32_t block_num)
{
    return FS_BASE_PTR + (block_num * BLOCK_SIZE);
}

uint32_t get_block_group_num(uint32_t block_num)
{
    return block_num / SUPER_BLOCK->blocks_per_group;
}

struct ext2_super_block *get_super_block()
{
    struct ext2_super_block *super_block;
    super_block = (struct ext2_super_block *)(FS_BASE_PTR + 1024);
    return super_block;
}

struct ext2_block_group_descriptor *get_block_group_descriptor(uint32_t block_group_num)
{
    struct ext2_block_group_descriptor *bg_descriptor;
    bg_descriptor = (struct ext2_block_group_descriptor *)(FS_BASE_PTR + EXT2_BOOT_BLOCK_SIZE + EXT2_SUPER_BLOCK_SIZE + block_group_num * 32);
    return bg_descriptor;
}

struct ext2_inode *get_inode(uint32_t inode_num)
{
    struct ext2_inode *inode;
    unsigned int group_num = (inode_num - 1) / SUPER_BLOCK->inodes_per_group;
    if (group_num < NUM_OF_BLOCK_GROUPS)
    {
        struct ext2_block_group_descriptor *bg_descriptor = get_block_group_descriptor(group_num);
        void *inode_table_ptr = get_block_pointer(bg_descriptor->inode_table);
        inode = (struct ext2_inode *)(inode_table_ptr + INODE_SIZE * ((inode_num - 1) % SUPER_BLOCK->inodes_per_group));
        return inode;
    }
    else
    {
        return nullptr;
    }
}

uint32_t get_inode_block_group_number(uint32_t inode_num)
{
    return (inode_num - 1) / SUPER_BLOCK->inodes_per_group;
}

void recover_inode_bitmap()
{
    // Loop through all block groups
    for (uint32_t i = 0; i < NUM_OF_BLOCK_GROUPS; i++)
    {
        struct ext2_block_group_descriptor *bg_descriptor = get_block_group_descriptor(i);

        uint8_t *inode_bitmap_ptr = get_block_pointer(bg_descriptor->inode_bitmap);
        uint8_t *inode_table_ptr = get_block_pointer(bg_descriptor->inode_table);

        uint32_t inodes_per_group = SUPER_BLOCK->inodes_per_group;
        uint32_t inode_bitmap_size = inodes_per_group / 8;
        uint8_t *inode_bitmap = (uint8_t *)inode_bitmap_ptr;

        for (uint32_t j = 0; j < inode_bitmap_size; j++)
        {
            uint8_t byte = *(inode_bitmap + j);
            for (uint32_t k = 0; k < 8; k++)
            {

                if (!(byte & (1 << k)))
                {
                    struct ext2_inode *inode = (struct ext2_inode *)(inode_table_ptr + INODE_SIZE * (j * 8 + k));

                    // print inode
                    if (inode->link_count != 0)
                    {
                        inode_bitmap[j] |= (1 << k);
                    }
                }
            }
        }
    }

    // Mark first 11 inodes as used in inode bitmap
    for (uint32_t i = 0; i < 11; i++)
    {
        struct ext2_block_group_descriptor *bg_descriptor = get_block_group_descriptor(0);
        uint8_t *inode_bitmap_ptr = get_block_pointer(bg_descriptor->inode_bitmap);
        uint8_t *inode_table_ptr = get_block_pointer(bg_descriptor->inode_table);
        uint8_t *inode_bitmap = (uint8_t *)inode_bitmap_ptr;
        inode_bitmap[0] |= 0xff;
        inode_bitmap[1] |= 0b00000111;
    }
}

void set_block_bitmap(uint32_t block_num)
{
    uint32_t block_group_num = get_block_group_num(block_num);
    struct ext2_block_group_descriptor *bg_descriptor = get_block_group_descriptor(block_group_num);
    uint8_t *block_bitmap_ptr = get_block_pointer(bg_descriptor->block_bitmap);
    uint8_t *block_bitmap = (uint8_t *)block_bitmap_ptr;
    block_num = block_num - block_group_num * SUPER_BLOCK->blocks_per_group;
    block_bitmap[block_num / 8] |= (1 << (block_num % 8));
}

void recover_block_bitmap_file_system()
{
    // Make sure that the block bitmap is set for all block till inode table block and inode bitmap block
    for (uint32_t i = 0; i < NUM_OF_BLOCK_GROUPS; i++)
    {
        struct ext2_block_group_descriptor *bg_descriptor = get_block_group_descriptor(i);

        uint8_t *block_bitmap_ptr = get_block_pointer(bg_descriptor->block_bitmap);
        uint8_t *block_bitmap = (uint8_t *)block_bitmap_ptr;

        uint32_t first_data_block_in_group = bg_descriptor->inode_table + INODE_TABLE_BLOCK_NUM;
        uint32_t group_start_block = i * SUPER_BLOCK->blocks_per_group;

        for (uint32_t j = group_start_block; j < first_data_block_in_group; j++)
        {
            set_block_bitmap(j);
        }
    }
}

bool is_block_empty(uint32_t block_num)
{
    // Get the block
    // Check if all the block is 0
    uint8_t *block_ptr = get_block_pointer(block_num);
    for (uint32_t i = 0; i < BLOCK_SIZE; i++)
    {
        if (block_ptr[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool is_directory_block(uint32_t block_num)
{
    uint8_t *block_ptr = get_block_pointer(block_num);
    struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *)block_ptr;
    uint32_t inode_num = dir_entry->inode;
    struct ext2_inode *inode = get_inode(inode_num);
    if (inode == nullptr)
    {

        if (inode_num == 0 && dir_entry->length == BLOCK_SIZE)
        {
            return true;
        }

        return false;
    }

    if (inode->link_count != 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void print_block_binary(u_int32_t block_num)
{
    // print in binary
    u_int8_t *block_ptr = get_block_pointer(block_num);
    cout << "Block " << block_num << ": ";
    for (u_int32_t i = 0; i < BLOCK_SIZE; i++)
    {
        cout << bitset<8>(block_ptr[i]) << " ";
    }
}
void recover_empty_blocks_bitmap()
{
    // Loop through all data blocks in all block groups
    // if the block start with the identifier, mark it as used in the block bitmap
    for (uint32_t i = 0; i < NUM_OF_BLOCK_GROUPS; i++)
    {
        struct ext2_block_group_descriptor *bg_descriptor = get_block_group_descriptor(i);

        uint8_t *block_bitmap_ptr = get_block_pointer(bg_descriptor->block_bitmap);
        uint8_t *block_bitmap = (uint8_t *)block_bitmap_ptr;

        // loop through all inodes in the inode table and set the block bitmap

        uint32_t inodes_per_group = SUPER_BLOCK->inodes_per_group;
        uint32_t inode_bitmap_size = inodes_per_group / 8;
        uint8_t *inode_bitmap = (uint8_t *)block_bitmap_ptr;
        u_int8_t *inode_table_ptr = get_block_pointer(bg_descriptor->inode_table);

        for (uint32_t j = 0; j < inode_bitmap_size; j++)
        {
            uint8_t byte = *(inode_bitmap + j);
            for (uint32_t k = 0; k < 8; k++)
            {
                if (byte & (1 << k))
                {
                    struct ext2_inode *inode = (struct ext2_inode *)(inode_table_ptr + INODE_SIZE * (j * 8 + k));

                    uint32_t block_count = inode->size / BLOCK_SIZE;
                    if (inode->size % BLOCK_SIZE != 0)
                    {
                        block_count++;
                    }

                    uint32_t *direct_blocks = inode->direct_blocks;

                    // loop through all direct blocks
                    for (uint32_t l = 0; l < 12; l++)
                    {

                        if (direct_blocks[l] != 0)
                        {

                            set_block_bitmap(direct_blocks[l]);
                        }
                    }

                    // loop through all indirect blocks
                    if (inode->single_indirect != 0)
                    {
                        uint32_t *block_ptr = (uint32_t *)get_block_pointer(inode->single_indirect);
                        set_block_bitmap(inode->single_indirect);
                        for (uint32_t l = 0; l < BLOCK_SIZE / 4; l++)
                        {
                            if (block_ptr[l] != 0)
                            {

                                set_block_bitmap(block_ptr[l]);
                            }
                        }
                    }

                    if (inode->double_indirect != 0)
                    {
                        uint32_t *block_ptr = (uint32_t *)get_block_pointer(inode->double_indirect);
                        set_block_bitmap(inode->double_indirect);
                        for (uint32_t l = 0; l < BLOCK_SIZE / 4; l++)
                        {
                            if (block_ptr[l] != 0)
                            {
                                uint32_t *block_ptr2 = (uint32_t *)get_block_pointer(block_ptr[l]);
                                set_block_bitmap(block_ptr[l]);
                                for (uint32_t m = 0; m < BLOCK_SIZE / 4; m++)
                                {
                                    if (block_ptr2[m] != 0)
                                    {
                                        if (is_block_empty(block_ptr2[m]))
                                        {
                                            set_block_bitmap(block_ptr2[m]);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (inode->triple_indirect != 0)
                    {
                        uint32_t *block_ptr = (uint32_t *)get_block_pointer(inode->triple_indirect);
                        set_block_bitmap(inode->triple_indirect);
                        for (uint32_t l = 0; l < BLOCK_SIZE / 4; l++)
                        {
                            if (block_ptr[l] != 0)
                            {
                                uint32_t *block_ptr2 = (uint32_t *)get_block_pointer(block_ptr[l]);
                                set_block_bitmap(block_ptr[l]);
                                for (uint32_t m = 0; m < BLOCK_SIZE / 4; m++)
                                {
                                    if (block_ptr2[m] != 0)
                                    {
                                        uint32_t *block_ptr3 = (uint32_t *)get_block_pointer(block_ptr2[m]);
                                        set_block_bitmap(block_ptr2[m]);
                                        for (uint32_t n = 0; n < BLOCK_SIZE / 4; n++)
                                        {
                                            if (block_ptr3[n] != 0)
                                            {
                                                if (is_block_empty(block_ptr3[n]))
                                                {
                                                    set_block_bitmap(block_ptr3[n]);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void recover_nonempty_blocks_bitmap()
{
    // Loop through all data blocks in all block groups
    // if the block start with the identifier, mark it as used in the block bitmap
    for (uint32_t i = 0; i < NUM_OF_BLOCK_GROUPS; i++)
    {
        struct ext2_block_group_descriptor *bg_descriptor = get_block_group_descriptor(i);

        uint32_t first_data_block_in_group = bg_descriptor->inode_table + INODE_TABLE_BLOCK_NUM;
        uint32_t last_data_block_in_group = (i + 1) * SUPER_BLOCK->blocks_per_group;

        for (uint32_t j = first_data_block_in_group; j < last_data_block_in_group; j++)
        {
            uint8_t *block_ptr = get_block_pointer(j);

            if (is_block_empty(j))
            {
                continue;
            }
            else if (memcmp(block_ptr, DATA_IDENTIFIER, IDENTIFIER_SIZE) == 0)
            {
                set_block_bitmap(j);
            }
            else if (is_directory_block(j))
            {
                set_block_bitmap(j);
            }
            else
            {
                set_block_bitmap(j);

                uint32_t *block_ptr = (uint32_t *)block_ptr;
                for (uint32_t k = 0; k < BLOCK_SIZE / 4; k++)
                {
                    if (block_ptr[k] != 0)
                    {
                        set_block_bitmap(block_ptr[k]);
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{

    FS_BASE_PTR = (uint8_t *)mmap_file_system(argv[1]);
    DATA_IDENTIFIER = parse_identifier(argc, argv);
    IDENTIFIER_SIZE = argc - 2;

    SUPER_BLOCK = get_super_block();
    BLOCK_SIZE = 1024 * (1 << SUPER_BLOCK->log_block_size);
    INODE_SIZE = SUPER_BLOCK->inode_size;
    NUM_OF_BLOCK_GROUPS = SUPER_BLOCK->block_count / SUPER_BLOCK->blocks_per_group;
    INODE_TABLE_BLOCK_NUM = SUPER_BLOCK->inodes_per_group * INODE_SIZE / BLOCK_SIZE;

    recover_inode_bitmap();
    recover_block_bitmap_file_system();
    recover_nonempty_blocks_bitmap();
    recover_empty_blocks_bitmap();
}
