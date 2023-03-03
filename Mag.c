#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

typedef uint8_t vector[4];//массив с 4 элементаами, каждый из которых хранит 8 бит 

vector iter_key[32];//используется в Magma_Expand_Key, в iter_key[32] хранятся 32 итерационных ключа K1...K8. 
//Каждому элементу соответсвует определенный итерационный ключ (определенаня 32-битная часть изначального ключа)
//так как тип массива iter_key[32] имеет тип vect, то есть это массив, состоящий из 32  массивов vect

//сложение по модулю 2, то есть XOR - исключающее или (понадобитья позже)
static void Add_mod_2(const uint8_t* input1, const uint8_t* input2, uint8_t* output) {
    for (int i = 0; i < 4; i++) {
        output[i] = input1[i] ^ input2[i];
    }
}

// сложение по модулю 32
static void Add_mod_32(const uint8_t* input1, const uint8_t* input, uint8_t* output) {
    uint8_t internal = 0;
    for (int i = 3; i >= 0; i--) {
        internal = input1[i] + input[i] + (internal >> 8);
        output[i] = internal & 0xff;
    }
}

//Табличка перестановок. В госте она представленна в обратном порядке (big endian), у нас litle endian.
//По ней смотришь в каком порядке ты меняешь на другое, что на что
static uint8_t Pi[8][16] =
{
        {1,  7,  14, 13, 0,  5,  8,  3,  4,  15, 10, 6,  9,  12, 11, 2},
        {8,  14, 2,  5,  6,  9,  1,  12, 15, 4,  11, 0,  13, 10, 3,  7},
        {5,  13, 15, 6,  9,  2,  12, 10, 11, 7,  8,  1,  4,  3,  14, 0},
        {7,  15, 5,  10, 8,  1,  6,  13, 0,  9,  3,  14, 11, 4,  2,  12},
        {12, 8,  2,  1,  13, 4,  15, 6,  7,  0,  10, 5,  3,  14, 9,  11},
        {11, 3,  5,  8,  2,  15, 10, 13, 14, 1,  7,  4,  12, 9,  6,  0},
        {6,  8,  2,  3,  9,  10, 5,  12, 1,  14, 4,  7,  11, 13, 0,  15},
        {12, 4,  6,  2,  10, 5,  11, 9,  14, 8,  13, 7,  0,  3,  15, 1}
};


//преобразование Т (См. блок схему 2 в курсовой)
static void Magma_T(const uint8_t* in_data, uint8_t* out_data) {
    uint8_t first_part_byte, sec_part_byte;

    for (int i = 0; i < 4; i++) {
        first_part_byte = (in_data[i] & 0xf0) >> 4;
        sec_part_byte = (in_data[i] & 0x0f);
        first_part_byte = Pi[i * 2][first_part_byte];
        sec_part_byte = Pi[i * 2 + 1][sec_part_byte];
        out_data[i] = (first_part_byte << 4) | sec_part_byte;
    }
}

//Сдесь мы получаем ключи (см. блок схему 3 в курсовой)
//В данном случии  memcpy копирует 4 байта (32 бита) из key(исходный 256 битный ключ) в iter_key[0] (K1- итерационный ключ)
//Затем следующие 32 бита (4 байта) из key и копирует в iter_key[1] (K2 из таблицы 3)
void Magma_Expand_Key(const uint8_t* key) {

    for (int i = 0; i <= 31; i++)
        for (int j = 0; j <= 7; j++) {
            memcpy(iter_key[i], key + 4 * j, 4);
        }

}

//Опираация g - объединение 1-ых трех опираций из блоксхемы 2 
//(то есть это сложение с ключом Ki, замена по таблице замены и цеклический сдвиг на 11), отлицается от опирации G в блоксхеме 2 отсутсвтем xor (сложение по мод 2 )
static void transformation_g(const uint8_t* k, const uint8_t* a, uint8_t* out_data) {
    uint8_t internal[4];
    uint32_t out_data_32;
    Add_mod_32(a, k, internal);
    Magma_T(internal, internal);
    out_data_32 = internal[0];
    out_data_32 = (out_data_32 << 8) + internal[1];
    out_data_32 = (out_data_32 << 8) + internal[2];
    out_data_32 = (out_data_32 << 8) + internal[3];
    out_data_32 = (out_data_32 << 11) | (out_data_32 >> 21);
    out_data[3] = out_data_32;
    out_data[2] = out_data_32 >> 8;
    out_data[1] = out_data_32 >> 16;
    out_data[0] = out_data_32 >> 24;
}

//Опирация G - для 1-ыx 31-одной итерации (это прям G из блоксхемы 2)
static void transformation_G(const uint8_t* k, const uint8_t* a, uint8_t* out_data) {
    uint8_t a_0[4];
    uint8_t a_1[4];
    uint8_t G[4];


    for (int i = 0; i < 4; i++) {
        a_0[i] = a[4 + i];
        a_1[i] = a[i];
    }

    transformation_g(k, a_0, G);
    Add_mod_2(a_1, G, G);

    for (int i = 0; i < 4; i++) {
        a_1[i] = a_0[i];
        a_0[i] = G[i];
    }
    for (int i = 0; i < 4; i++) {
        out_data[i] = a_1[i];
        out_data[4 + i] = a_0[i];
    }

}

// То же самое, что и G обычная, но только для 32-ой итерации 
static void transformation_G_number_32(const uint8_t* k, const uint8_t* a, uint8_t* out_data) {
    uint8_t a_0[4];
    uint8_t a_1[4];
    uint8_t G[4];


    for (int i = 0; i < 4; i++) {
        a_0[i] = a[4 + i];
        a_1[i] = a[i];
    }

    transformation_g(k, a_0, G);
    Add_mod_2(a_1, G, G);
    for (int i = 0; i < 4; i++)
        a_1[i] = G[i];

    for (int i = 0; i < 4; i++) {
        out_data[i] = a_1[i];
        out_data[4 + i] = a_0[i];
    }
}

//Идет шифрование 
void Magma_Encrypt(const uint8_t* input, uint8_t* output) {
    transformation_G(iter_key[0], input, output);
    for (int i = 1; i < 31; i++)
        transformation_G(iter_key[i], output, output);
    transformation_G_number_32(iter_key[31], output, output);
}


//Расшифрование (по сути расшифровка в магме, то же алгоритм для шифровки, но только на оборот)
void Magma_Decrypt(const uint8_t* input, uint8_t* output) {
    transformation_G(iter_key[31], input, output);
    for (int i = 30; i > 0; i--)
        transformation_G(iter_key[i], output, output);
    transformation_G_number_32(iter_key[0], output, output);
}

static uint8_t test_key[32] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
};

//здесь поределены те файлы, от куда считываем и от куда пишем
// Оперделяем размер блоков для шифрованиея, дополняем их, если размер не кратен 64
int main(int argc, char* argv[]) {

    FILE* fpRead;
    FILE* fpWrite;
    FILE* fpKey;

    if (argc >= 3) {
        fpRead = fopen(argv[2], "r");
        fpKey = fopen(argv[1], "r");

        //Узнаем размер файла (если есть остаток, округляем в большую сторону)
        fseek(fpKey, 0L, SEEK_END);
        long maxSizeKey = ftell(fpKey);
        fseek(fpKey, 0, SEEK_SET);

        if (maxSizeKey < 32) {
            perror("The key is too small");
            return -2;
        }
        fread(test_key, 1, 32, fpKey);
    }

    else {
        perror("Not enough parameters have been entered");
        return -1;
    }

    int counter;
    printf("If you want to encrypt the file specified at startup, enter \"1\" \n");
    printf("If you want to decrypt the file specified at startup, enter \"2\" \n");
    printf("If you want to decrypt the output, enter \"-1\" \n");
    scanf("%d", &counter);
    

    if (counter == 1) {

        fpWrite = fopen("afte.txt", "w");
        if (fpWrite == NULL) {
            perror("Error opening output file");
            return -2;
        }

        fseek(fpRead, 0, SEEK_SET);
        fseek(fpRead, 0L, SEEK_END);
        long maxSize = ftell(fpRead);
        fseek(fpRead, 0, SEEK_SET);

        long numberOfBlock64 = 0;;

        if (maxSize % 8 == 0) {
            numberOfBlock64 = maxSize / 8;
        }
        else {
            numberOfBlock64 = (maxSize / 8) + 1;
        }

        Magma_Expand_Key(test_key);

        uint8_t fileBlock[8];
        uint8_t fileBlockForRead[8];

        uint8_t initVector[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        int randomData = open("/dev/urandom", O_RDONLY);
        if (randomData == 0) {
            return -5;
        }
        else {
            ssize_t result = read(randomData, initVector, (sizeof(initVector)));
            if (result < 0) {
                return -6;
            }
        }

        fwrite(initVector, 1, 8, fpWrite);

        //Здесь реализован режим доступа CFB
        for (int i = 0; i < numberOfBlock64; i++) {
            Magma_Encrypt(initVector, fileBlock);

            memset(fileBlockForRead, 0, sizeof(fileBlockForRead));//по сути забивает 0-ми блок, что бы он делился на 64 бита (речь идет об исходном файле)

            fread(fileBlockForRead, 1, 8, fpRead);
            for (int j = 0; j < 8; j++) {
                initVector[j] = fileBlock[j] ^ fileBlockForRead[j];
            }



            fwrite(initVector, 1, 8, fpWrite);

        }

        fclose(fpRead);
        fclose(fpWrite);
        fclose(fpKey);

    }

    else if (counter == 2) {
        fpWrite = fopen("befor.txt", "w");
        if (fpWrite == NULL) {
            perror("Error opening output file");
            return -2;
        }

        fseek(fpRead, 0, SEEK_SET);
        fseek(fpRead, 0L, SEEK_END);
        long maxSize = ftell(fpRead);
        fseek(fpRead, 0, SEEK_SET);

        long numberOfBlock64 = 0;;

        if (maxSize % 8 == 0) {
            numberOfBlock64 = maxSize / 8 - 1;
        }
        else {
            numberOfBlock64 = (maxSize / 8);
        }

        Magma_Expand_Key(test_key);

        uint8_t fileBlock[8];
        uint8_t fileBlockForRead[8];

        uint8_t initVector[8];

        fread(initVector, 1, 8, fpRead);

        //Здесь реализован режим доступа CFB
        for (int i = 0; i < numberOfBlock64; i++) {
            Magma_Encrypt(initVector, fileBlock);

            memset(fileBlockForRead, 0, sizeof(fileBlockForRead));//по сути забивает 0-ми блок, что бы он делился на 64 бита (речь идет об исходном файле)

            fread(fileBlockForRead, 1, 8, fpRead);
            for (int j = 0; j < 8; j++) {
                initVector[j] = fileBlock[j] ^ fileBlockForRead[j];
            }


            fwrite(initVector, 1, 8, fpWrite);

            memcpy(initVector, fileBlockForRead, sizeof(fileBlockForRead));
        }

        fclose(fpRead);
        fclose(fpWrite);
        fclose(fpKey);
    }

    else if (counter == -1)
    return 0;

    else
    printf("You have entered an incorrect character, restart the program and try again");
   

    return 0;
}
