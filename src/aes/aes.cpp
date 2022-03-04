/*
 * Copyright (c) 2022, Tobias <git@tsmr.eu>
 * 
 */

#include "aes.h"
#include <stdio.h>

const byte SBox[16][16] = {
    {0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76},
    {0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0},
    {0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15},
    {0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75},
    {0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84},
    {0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf},
    {0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8},
    {0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2},
    {0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73},
    {0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb},
    {0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79},
    {0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08},
    {0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a},
    {0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e},
    {0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf},
    {0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16}
};


void print_state(byte * state) {
    for (byte x = 0; x < 4; x++)
    {
        printf("%d [", x);
        for (byte y = 0; y < 4; y++)
        {
            printf("%02x", state[(y*4) + x]);
            if (y <= 2)
                printf(" ");
        }
        printf("]\n");
    }

};

void AES::SubBytes() {

    for (byte i = 0; i < 16; i++)
        m_state[i] = SBox[(m_state[i] >> 4) & 0xf][(m_state[i]) & 0xf];

}

void AES::SubWord(byte * word) {

    for (byte i = 0; i < 4; i++)
        word[i] = SBox[(word[i] >> 4) & 0xf][(word[i]) & 0xf];

}

void AES::RotWord(byte * word) {
    byte tmp = word[0];
    word[0] = word[1];
    word[1] = word[2];
    word[2] = word[3];
    word[3] = tmp;
}

void AES::update_key(byte * key) {

    byte temp[4];

    const byte Nk = m_blocksize / 32; // (Key-Length in words (4 Bytes))
    const byte Nr = m_blocksize / 32 + 6;

    const byte Rcon[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};

    memcpy(m_expanded_key, key, 32);

    // for (size_t i = 0; i < 4; i++)
    // {
    //     printf("w%zu = ", i);
    //     for (size_t j = 0; j < 4; j++) 
    //         printf("%2x", m_expanded_key[i][j]);
    //     printf("\n");
    // }

    for (size_t i = Nk; i < 4*(Nr+1); i++)
    {
        memcpy(temp, m_expanded_key[i-1], 4);

        // printf("temp = ");
        // for (size_t j = 0; j < 4; j++)
        //     printf("%2x", temp[j]);
        // printf("\n");

        if (i % Nk == 0) {

            RotWord(temp);
            // printf("after RotWord() = ");
            // for (size_t j = 0; j < 4; j++)
            //     printf("%2x", temp[j]);
            // printf("\n");

            SubWord(temp);

            // printf("after SubWord() = ");
            // for (size_t j = 0; j < 4; j++)
            //     printf("%2x", temp[j]);
            // printf("\n");
            
            temp[0] ^= Rcon[i/Nk-1];

            // printf("after XOR with Rcon %2x = ");
            // for (size_t j = 0; j < 4; j++)
            //     printf("%2x", temp[j]);
            // printf("\n");

        } else if (Nk > 6 && i % Nk == 4) {
            SubWord(temp);
        }
        for (byte j = 0; j < 4; j++)
            m_expanded_key[i][j] = m_expanded_key[i-Nk][j] ^ temp[j];
  
    }

}

void AES::AddRoundKey(byte round) {

    for (byte x = 0; x < 4; x++)
    {
        for (byte y = 0; y < 4; y++)
            m_state[(x*4) + y] = m_state[(x*4) + y] ^ m_expanded_key[(round*4)+x][y];   
    }

}

void AES::ShiftRows() {

    for (byte shift = 1; shift < 4; shift++)
    {
        for (byte i = 0; i < shift; i++)
        {
            byte tmp = m_state[shift];
            m_state[shift] = m_state[shift+4];
            m_state[shift+4] = m_state[shift+8];
            m_state[shift+8] = m_state[shift+12];
            m_state[shift+12] = tmp;
        }
    }
}

byte multiplication_of_polynomials_modulo(byte a, byte b) {

    byte k[8]{};

    for (byte i = 0; i < 8; i++)
    {
        k[i] = a * (0b1 << (i+1)) & 0xff;
        if (i >= 1) {
            if ((k[i-1] >> 7) == 0b1) {
                k[i] = k[i] ^ 0b00011011;
            }
        }
    }

    byte c = 0b0;

    if ((b & 0b1) == 0b1) {
        c ^= a;
    }

    for (byte i = 0; i < 8; i++)
    {
        if((b >> (i+1) & 0b1) == 0b1) {
            c ^= k[i];
        }
    }
    
    return c;

}

void AES::MixColumns() {

    byte matrix[] =
       {0x02, 0x03, 0x01, 0x01,
        0x01, 0x02, 0x03, 0x01,
        0x01, 0x01, 0x02, 0x03,
        0x03, 0x01, 0x01, 0x02};

    byte tmp_matrix[16];

    for (byte column = 0; column < 4; column++)
    {
        for (byte row = 0; row < 4; row++)
        {
            uint32_t tmp = 0;
            for (byte matrix_column = 0; matrix_column < 4; matrix_column++)
            {
                // if (matrix_column > 0)
                //     printf(" + ");
                byte a = multiplication_of_polynomials_modulo(matrix[(row*4)+matrix_column], m_state[matrix_column + (4*column)]);
                tmp ^= a;
                // printf("(%d * %d [%d])", matrix[(row*4)+matrix_column], m_state[column + (4*matrix_column)], a);
            }

            // printf(" = %x\n", tmp);
            tmp_matrix[row + (4*column)] = tmp;
            
        }
        
    }
    
    memcpy(m_state, tmp_matrix, 16);

}

bool AES::encrypt(byte * input, byte * output) {

    memcpy(m_state, input, 16);

    byte rounds = m_blocksize / 32 + 6;

    printf("\n\n--- RUNDE 0 ----\n\n");
    print_state(m_state);
    AddRoundKey(0);

    for (byte round = 1; round <= rounds; round++)
    {
        printf("\n\n--- RUNDE %d ----\n\n", round);
        print_state(m_state);
        SubBytes();
        printf("After SubBytes()\n");
        print_state(m_state);

        ShiftRows();
        printf("After ShiftRows()\n");
        print_state(m_state);
        if (round <= rounds-1) {
            MixColumns();
            printf("After MixColumns()\n");
            print_state(m_state);
        }
        AddRoundKey(round);
    }

    memcpy(output, m_state, 16);
    memset(m_state, 0x00, 16);

    return true;

}

bool AES::decrypt(byte * input, byte * output) {

    return true;
}
