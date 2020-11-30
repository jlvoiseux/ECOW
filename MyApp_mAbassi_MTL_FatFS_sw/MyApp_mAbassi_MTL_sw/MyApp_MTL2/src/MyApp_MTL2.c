/*-----------------------------------------------------------
 *
 * MyApp - mAbassi
 *
 *-----------------------------------------------------------*/

#include "../inc/MyApp_MTL2.h"

#include "mAbassi.h"  /* MUST include "SAL.H" and not uAbassi.h        */
#include "Platform.h" /* Everything about the target platform is here  */
#include "HWinfo.h"   /* Everything about the target hardware is here  */
#include "SysCall.h"  /* System Call layer stuff                        */

#include "arm_pl330.h"
#include "dw_i2c.h"
#include "cd_qspi.h"
#include "dw_sdmmc.h"
#include "dw_spi.h"
#include "dw_uart.h"
#include "alt_gpio.h"
#include "arm_acp.h"

#include "gui.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include "ppm.h"

#define USE_ACP 0

MTC2_INFO *myTouch;
VIP_FRAME_READER *myReader;
uint32_t *myFrameBuffer;
uint32_t *previousBuffer;

int diskReady = 0;
Image landscape;
Image bu_landscape;
int countOnes = 0;
int countBeats = 0;
int* pos;
char beatArray[20000];
int role = 0; // 1 : master, 2 : slave 
int score = 0;
int state =0;
int loadingImages = 0;
int songInt = 0;
int isSongSelected = 0;
int songSentToNios = 0;
int niosFinished = 0;
int spiNiosReady = 0;
int gameFinished = 0;
int winStatus = 0; // 1 : win, 2 : lose, 3 : egalite
int winStatus2 = 0; // 1 : win, 2 : lose, 3 : egalite

/*-----------------------------------------------------------*/

void Task_DMA(void)
{
    MTX_t    *PrtMtx;       // Mutex for exclusive access to printf()
    SEM_t    *PtrSem;

    FILE *fileptra;
    char *buffera;
    long filelena;    

    FILE *fileptrb;
    char *bufferb;
    long filelenb; 
    
    PrtMtx = MTXopen("Printf Mtx");
    PtrSem = SEMopen("MySem_DMA");
    
    for( ;; )
    {
        SEMwait(PtrSem, -1);    // -1 = Infinite blocking
        SEMreset(PtrSem);
        MTXlock(PrtMtx, -1);
        printf("\nStarting DMA on core %d\n", COREgetID());
        if(songInt == 1){
            fileptra = fopen("songs/fh/fha.bin", "rb");
            printf("Song A\n");
        }
        else if(songInt == 2){
            fileptra = fopen("songs/sh/sha.bin", "rb");
            printf("Song B\n");
        }
        fseek(fileptra, 0, SEEK_END);
        filelena = ftell(fileptra); 
        rewind(fileptra);
        buffera = (char *)malloc((filelena+1)*sizeof(char));
        fread(buffera, filelena, 1, fileptra);
        fclose(fileptra);
        for(int i = 0; i < filelena; i++){
            memset((void *) 0x22000000+i, *(buffera + i), 1);
            // if(i == 3537875){
            //     printf("Test : %x\n", *(buffera + i));
            // }
        }
        
        printf("File a read : Buffer  %x - Length %d\n", buffera, filelena);
        memset((void *) 0x21000000, ((uintptr_t)buffera & 0xff000000)>>24, 1);
        memset((void *) 0x21000001, ((uintptr_t)buffera & 0x00ff0000)>>16, 1);
        memset((void *) 0x21000002, ((uintptr_t)buffera & 0x0000ff00)>>8, 1);
        memset((void *) 0x21000003, ((uintptr_t)buffera & 0x0000000ff), 1);
        memset((void *) 0x21000010, (filelena & 0xff000000)>>24, 1);
        memset((void *) 0x21000011, (filelena & 0x00ff0000)>>16, 1);
        memset((void *) 0x21000012, (filelena & 0x0000ff00)>>8, 1);
        memset((void *) 0x21000013, (filelena & 0x000000ff), 1);

        free(buffera);

        if(songInt == 1){
            fileptrb = fopen("songs/fh/fhb.bin", "rb");
            printf("Song A\n");
        }
        else if(songInt == 2){
            fileptrb = fopen("songs/sh/shb.bin", "rb");
            printf("Song B\n");
        }
        fseek(fileptrb, 0, SEEK_END);
        filelenb = ftell(fileptrb); 
        rewind(fileptrb);
        bufferb = (char *)malloc((filelenb+1)*sizeof(char));
        fread(bufferb, filelenb, 1, fileptrb);
        fclose(fileptrb);
        for(int i = 0; i < filelenb; i++){
            memset((void *) 0x23000000+i, *(bufferb + i), 1);
            // if(i == 3537875){
            //     printf("Test : %x\n", *(buffera + i));
            // }
        }

        printf("File b read : Buffer  %x - Length %d\n", bufferb, filelenb);
        memset((void *) 0x21000020, ((uintptr_t)bufferb & 0xff000000)>>24, 1);
        memset((void *) 0x21000021, ((uintptr_t)bufferb & 0x00ff0000)>>16, 1);
        memset((void *) 0x21000022, ((uintptr_t)bufferb & 0x0000ff00)>>8, 1);
        memset((void *) 0x21000023, ((uintptr_t)bufferb & 0x0000000ff), 1);
        memset((void *) 0x21000030, (filelenb & 0xff000000)>>24, 1);
        memset((void *) 0x21000031, (filelenb & 0x00ff0000)>>16, 1);
        memset((void *) 0x21000032, (filelenb & 0x0000ff00)>>8, 1);
        memset((void *) 0x21000033, (filelenb & 0x000000ff), 1);

        free(bufferb);

        memset((void *) 0x25000000, 0xCC, 1);

               
        
        //printf("Test : %x at %x\n", *(DMA_Dst), (DMA_Dst));
        
//         Tick = G_OStimCnt;        
//         i=0;        
        
//         DMA_OpMode[i++] = DMA_CFG_EOT_ISR;
// #if (USE_ACP == 1)
//         DMA_OpMode[i++] = DMA_CFG_NOCACHE_SRC;
//         DMA_OpMode[i++] = DMA_CFG_NOCACHE_DST;
// #endif
//         DMA_OpMode[i++] = 0;
        
//         DMA_Err = dma_xfer(0,
//                            (uint8_t *)(ACPwrt + (uintptr_t) DMA_Dst), 1, MEMORY_DMA_ID,
//                            (uint8_t *)(ACPrd  + (uintptr_t) DMA_Src), 1, MEMORY_DMA_ID,
//                            1, 1, DMA_Size,
//                            DMA_OPEND_NONE, NULL, (intptr_t)0,
//                            &DMA_OpMode[0], &DMA_XferID, OS_MS_TO_TICK(1000));
        
//         Tick = G_OStimCnt - Tick;
        
//         if (DMA_Err != 0) {
//             printf("\ndma_xfer() reported the error #%d\n", DMA_Err);
//         } else {
//             Err = 0;
//             for (i=0; i<DMA_Size; i++)
//                 if (*(DMA_Src+i) != *(DMA_Dst+i)) {
//                     Err++;
//                     if (Err < 5)
//                         printf("Error in DMA transfert : %x instead of %x at %x (%x)\n", *(DMA_Dst+i), *(DMA_Src+i), (DMA_Dst+i), i);
//                 }
//             printf("DMA Test: Size: %d - Xfer: %d ms - %d MB/s \n\n", DMA_Size, (OS_TIMER_US*Tick)/1000, (Tick == 0) ? 0 : DMA_Size/((OS_TIMER_US*Tick)));
//         }
        MTXunlock(PrtMtx);
        
    }
}

/*-----------------------------------------------------------*/

void Task_State(void){
    for(;;){
        if(alt_read_word(fpga_spi) == 0xffffffff && role == 0){
            role = 2;
            printf("Slave");
        }
        else  if(alt_read_word(fpga_spi) == 0x22222222 && role == 0){
            role = 1;
            printf("Master");
        }
        if(diskReady && loadingImages && state == 0 && role != 0){
            state = 1;            
            alt_write_word(SPI_TXDATA, 0);
            printf("State set to 1");
        }
        else if(isSongSelected && state == 1){
            alt_write_word(SPI_TXDATA, songInt);
            state = 2;
            printf("State set to 2");
        }
        else if(niosFinished && state == 2){
            state = 3;
            alt_write_word(SPI_TXDATA, 4);
            printf("State set to 3");
        }
        else if(state == 3){
            state = 4;
            memset((void *) 0x25000010, 0xCC, 1);
            printf("State set to 4");
        }
        else if(gameFinished && state == 4){
            memset((void *) 0x25000000, 0xCD, 1);
            printf("Game Stop");
            state = 5;
            if(score == 4){
                score = 3;
            }
            alt_write_word(SPI_TXDATA, score);
            printf("State set to 5");
        }
        else if(winStatus != 0 && state == 5){
            state = 6;
            printf("State set to 6");
        }
        TSKsleep(OS_MS_TO_TICK(100));
    }
}

/*-----------------------------------------------------------*/

void Task_MTL2(void)
{
    MTX_t *PrtMtx;
    PrtMtx = MTXopen("Printf Mtx");

    myFrameBuffer = 0x20000000;

    TSKsleep(OS_MS_TO_TICK(500));

    // Initialize global timer
    assert(ALT_E_SUCCESS == alt_globaltmr_init());
    assert(ALT_E_SUCCESS == alt_globaltmr_start());

    MTXlock(PrtMtx, -1);
    printf("Starting MTL2 initialization\n");

    oc_i2c_init(fpga_i2c);
    myTouch = MTC2_Init(fpga_i2c, fpga_mtc2, LCD_TOUCH_INT_IRQ);

    // Enable IRQ for SPI & MTL

    OSisrInstall(GPT_SPI_IRQ, (void *)&spi_CallbackInterrupt);
    GICenable(GPT_SPI_IRQ, 128, 1);
    OSisrInstall(GPT_MTC2_IRQ, (void *)&mtc2_CallbackInterrupt);
    GICenable(GPT_MTC2_IRQ, 128, 1);
    alt_write_word(SPI_CONTROL, SPI_CONTROL_IRRDY + SPI_CONTROL_IE);
    alt_write_word(SPI_TXDATA, 0x69696969);
    // Enable interruptmask and edgecapture of PIO core for mtc2 irq
    alt_write_word(PIOinterruptmask_fpga_MTL, 0x3);
    alt_write_word(PIOedgecapture_fpga_MTL, 0x3);

    // Mount drive 0 to a mount point
    if (0 != mount(FS_TYPE_NAME_AUTO, "/", 0, "0"))
    {
        printf("ERROR: cannot mount volume 0\n");
    }
    // List the current directory contents
    cmd_ls();

    printf("\nMTL2 initialization completed\n");
    MTXunlock(PrtMtx);

    diskReady = 1;
    srand(11631400);

    for (;;)
    {
        pos = GUI(myTouch, rand() % 6);
        //printf("%d, %d, %d, %d, %d\n", *pos, *(pos+1), *(pos+2), *(pos+3), *(pos+4));
        TSKsleep(OS_MS_TO_TICK(10));
    }
}

// Used by i2C_core.c

void delay_us(uint32_t us)
{
    uint64_t start_time = alt_globaltmr_get64();
    uint32_t timer_prescaler = alt_globaltmr_prescaler_get() + 1;
    uint64_t end_time;
    alt_freq_t timer_clock;

    assert(ALT_E_SUCCESS == alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock));
    end_time = start_time + us * ((timer_clock / timer_prescaler) / ALT_MICROSECS_IN_A_SEC);

    while (alt_globaltmr_get64() < end_time)
        ;
}

/*-----------------------------------------------------------*/

/* Align on cache lines if cached transfers */
static char g_Buffer[9600] __attribute__((aligned(OX_CACHE_LSIZE)));

void Task_DisplayFile(void)
{
    SEM_t *PtrSem;
    MBX_t    *PrtMbx;
    intptr_t *PtrMsg;
    PtrSem = SEMopen("MySem_DMA");
    PrtMbx = MBXopen("bulletMailbox", 128);
    int FdSrc;
    int Nrd0;
    int width;
    int height;
    uint32_t pixel;
    int i;
    int j;
    int count = 0;
    int countCow = 0;
    int fingerFlag1 = 0;
    int fingerFlag2= 0;
    int bulletArray[24][4] = {{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1},{-1,-1,-1,-1}};
    int bulletIndex = 0;
    int bulletDir = 0;
    int targetTime = 1;
    int spawnX = 380;
    int spawnY = 220;
    int objX1 = 40;
    int objX2 = 40;
    int objX3 = 40;
    int objX4 = 680;
    int objX5 = 680;
    int objX6 = 680;
    int objY1 = 40;
    int objY2 = 200;
    int objY3 = 360;
    int objY4 = 40;
    int objY5 = 200;
    int objY6 = 360;
    int bulletDist = 0;
    int touchDist;
    int speed1 = (sqrt(((spawnX - objX1)*(spawnX - objX1))+((spawnY - objY1)*(spawnY - objY1)))*20)/(600);    
    int speed2 = (sqrt(((spawnX - objX2)*(spawnX - objX2))+((spawnY - objY2)*(spawnY - objY2)))*20)/(600);
    int speed3 = (sqrt(((spawnX - objX3)*(spawnX - objX3))+((spawnY - objY3)*(spawnY - objY3)))*20)/(600);
    int speed4 = (sqrt(((spawnX - objX4)*(spawnX - objX4))+((spawnY - objY4)*(spawnY - objY4)))*20)/(600);
    int speed5 = (sqrt(((spawnX - objX5)*(spawnX - objX5))+((spawnY - objY5)*(spawnY - objY5)))*20)/(600);
    int speed6 = (sqrt(((spawnX - objX6)*(spawnX - objX6))+((spawnY - objY6)*(spawnY - objY6)))*20)/(600);
    Image cow0;
    Image cow1;
    Image cow2;
    Image cow3;
    Image cow4;
    Image cow5; 
    Image circle0;
    Image circle1;
    Image circle2;
    Image bullet0;
    Image bullet1;
    Image bullet2;
    Image menu1;
    Image win;
    Image lose;
    Image loadingBg;
    srand(11631400);
    //PtrSem = SEMopen("MySem_DisplayFile");

    for (;;)
    {        
        if(diskReady == 1){
            //SEMwait(PtrSem, -1); // -1 = Infinite blocking
            //SEMreset(PtrSem);
            if(loadingImages == 0){
                landscape = *ImageRead("bg/landscape.ppm");
                bu_landscape = *ImageRead("bg/landscape.ppm");
                menu1 = *ImageRead("bg/menu.ppm");
                win = *ImageRead("bg/win_screen.ppm");
                lose = *ImageRead("bg/lose_screen.ppm");
                loadingBg = *ImageRead("bg/loading.ppm");
                cow0 = *ImageRead("cow/cow0.ppm");
                cow1 = *ImageRead("cow/cow1.ppm");
                cow2 = *ImageRead("cow/cow2.ppm");
                cow3 = *ImageRead("cow/cow3.ppm");
                cow4 = *ImageRead("cow/cow4.ppm");
                cow5 = *ImageRead("cow/cow5.ppm");
                circle0 = *ImageRead("circles/circle0.ppm");
                circle1 = *ImageRead("circles/circle1.ppm");
                circle2 = *ImageRead("circles/circle2.ppm");
                bullet0 = *ImageRead("bullets/bullet0.ppm");
                bullet1 = *ImageRead("bullets/bullet1.ppm");
                bullet2 = *ImageRead("bullets/bullet2.ppm");
                loadingImages = 1;
            }
            
            if (state==1){
                if(!isSongSelected && role == 1){
                    AddFullImage(&menu1, 0, 0); 
                    if(*(pos+1) > 1 && *(pos+1) < 400){
                        isSongSelected = 1;
                        songInt = 1;
                    }
                    else if(*(pos+1) > 400 && *(pos+1) < 800){
                        isSongSelected = 1;
                        songInt = 2;
                    }                    
                }
                else{
                    if(songInt == 1){
                        //AddFullImage(&menu3, 0, 0); 
                    } 
                    else if(songInt == 2){
                        //AddFullImage(&menu2, 0, 0); 
                    }
                    else{
                        AddFullImage(&loadingBg, 0, 0);
                    }
                }
            }
            if(state==2){
                AddFullImage(&loadingBg, 0, 0); 
                if(!songSentToNios){
                    SEMpost(PtrSem);
                    songSentToNios = 1;
                }
                
            }
            if(state == 4){
                AddFullImage(&bu_landscape, 0, 0);                

                if(*(pos) != 0){
                    if(*(pos) == 1){
                        if(sqrt(((*(pos+1) - 400)*(*(pos+1) - 400))+((*(pos+2) - 380)*(*(pos+2) - 380))) < 80){
                            fingerFlag1 = 1;
                        }
                        fingerFlag2 = 0;
                    }
                    else{
                        if(sqrt(((*(pos+1) - 400)*(*(pos+1) - 400))+((*(pos+2) - 380)*(*(pos+2) - 380))) < 80){
                            fingerFlag1 = 1;
                        }
                        if(sqrt(((*(pos+3) - 400)*(*(pos+3) - 400))+((*(pos+4) - 380)*(*(pos+4) - 380))) < 80){
                            fingerFlag2 = 1;
                        }       
                    }
                }
                else{
                    fingerFlag1 = 0;
                    fingerFlag2 = 0;
                }

                if(fingerFlag1){
                    AddImage(&circle0, *(pos+1)-50, *(pos+2)-50);
                }
                if(fingerFlag2){
                    AddImage(&circle0, *(pos+3)-50, *(pos+4)-50); 
                }

                if (MBXget(PrtMbx, PtrMsg, 0) == 0){
                    bulletDir = rand() % 6;
                    for(int k = 0; k < 24; k++){
                        if(bulletArray[k][0] == -1){
                            bulletIndex = k;
                            bulletArray[bulletIndex][0] = 1;
                            bulletArray[bulletIndex][1] = bulletDir;
                            bulletArray[bulletIndex][2] = spawnX;
                            bulletArray[bulletIndex][3] = spawnY;
                            break;
                        }
                    }
                    
                }

                for(int k = 0; k < 24; k++){
                    if(bulletArray[k][0] != -1){
                        switch(bulletArray[k][1]){
                            case 0:
                                bulletDist = sqrt(((bulletArray[k][2] - objX1)*(bulletArray[k][2] - objX1))+((bulletArray[k][3] - objY1)*(bulletArray[k][3] - objY1)));
                                if(bulletDist < 100){
                                    bulletArray[k][0] = 3;
                                    touchDist = sqrt(((*(pos+1) - objX1)*(*(pos+1) - objX1))+((*(pos+2) - objY1)*(*(pos+2) - objY1)));
                                    if(touchDist < 60 && bulletDist < 60 && *pos != 0){
                                        score++;
                                        printf("Score : %d", score);
                                        bulletArray[k][0] = -1;
                                        bulletArray[k][1] = -1;
                                        bulletArray[k][2] = -1;
                                        bulletArray[k][3] = -1;
                                    }
                                }
                                else if(bulletDist < 250){
                                    bulletArray[k][0] = 2;
                                }
                                else if(bulletDist < 400){
                                    bulletArray[k][0] = 1;
                                }
                                bulletArray[k][2] -= speed1;
                                bulletArray[k][3] -= speed1/2;
                                break;
                            case 1:
                                bulletDist = sqrt(((bulletArray[k][2] - objX2)*(bulletArray[k][2] - objX2))+((bulletArray[k][3] - objY2)*(bulletArray[k][3] - objY2)));
                                if(bulletDist < 100){
                                    bulletArray[k][0] = 3;
                                    touchDist = sqrt(((*(pos+1) - objX2)*(*(pos+1) - objX2))+((*(pos+2) - objY2)*(*(pos+2) - objY2)));
                                    if(touchDist < 60 && bulletDist < 60  && *pos != 0){
                                        score++;
                                        printf("Score : %d", score);
                                        bulletArray[k][0] = -1;
                                        bulletArray[k][1] = -1;
                                        bulletArray[k][2] = -1;
                                        bulletArray[k][3] = -1;
                                    }
                                }
                                else if(bulletDist < 250){
                                    bulletArray[k][0] = 2;
                                }
                                else if(bulletDist < 400){
                                    bulletArray[k][0] = 1;
                                }
                                bulletArray[k][2] -= speed2;
                                break;
                            case 2:
                                bulletDist = sqrt(((bulletArray[k][2] - objX3)*(bulletArray[k][2] - objX3))+((bulletArray[k][3] - objY3)*(bulletArray[k][3] - objY3)));
                                if(bulletDist < 100){
                                    bulletArray[k][0] = 3;
                                    touchDist = sqrt(((*(pos+1) - objX3)*(*(pos+1) - objX3))+((*(pos+2) - objY3)*(*(pos+2) - objY3)));
                                    if(touchDist < 60 && bulletDist < 60 && *pos != 0){
                                        score++;
                                        printf("Score : %d", score);
                                        bulletArray[k][0] = -1;
                                        bulletArray[k][1] = -1;
                                        bulletArray[k][2] = -1;
                                        bulletArray[k][3] = -1;
                                    }
                                }
                                else if(bulletDist < 250){
                                    bulletArray[k][0] = 2;
                                }
                                else if(bulletDist < 400){
                                    bulletArray[k][0] = 1;
                                }
                                bulletArray[k][2] -= speed3;
                                bulletArray[k][3] += speed3/2;
                                break;
                            case 3:
                                bulletDist = sqrt(((bulletArray[k][2] - objX4)*(bulletArray[k][2] - objX4))+((bulletArray[k][3] - objY4)*(bulletArray[k][3] - objY4)));
                                if(bulletDist < 100){
                                    bulletArray[k][0] = 3;
                                    touchDist = sqrt(((*(pos+1) - objX4)*(*(pos+1) - objX4))+((*(pos+2) - objY4)*(*(pos+2) - objY4)));
                                    if(touchDist < 60 && bulletDist < 60 && *pos != 0){
                                        score++;
                                        printf("Score : %d", score);
                                        bulletArray[k][0] = -1;
                                        bulletArray[k][1] = -1;
                                        bulletArray[k][2] = -1;
                                        bulletArray[k][3] = -1;
                                    }
                                }
                                else if(bulletDist < 250){
                                    bulletArray[k][0] = 2;
                                }
                                else if(bulletDist < 400){
                                    bulletArray[k][0] = 1;
                                }
                                bulletArray[k][2] += speed4;
                                bulletArray[k][3] -= speed4/2;
                                break;
                            case 4:
                                bulletDist = sqrt(((bulletArray[k][2] - objX5)*(bulletArray[k][2] - objX5))+((bulletArray[k][3] - objY5)*(bulletArray[k][3] - objY5)));
                                if(bulletDist < 100){
                                    bulletArray[k][0] = 3;
                                    touchDist = sqrt(((*(pos+1) - objX5)*(*(pos+1) - objX5))+((*(pos+2) - objY5)*(*(pos+2) - objY5)));
                                    if(touchDist < 60 && bulletDist < 60 && *pos != 0){
                                        score++;
                                        printf("Score : %d", score);
                                        bulletArray[k][0] = -1;
                                        bulletArray[k][1] = -1;
                                        bulletArray[k][2] = -1;
                                        bulletArray[k][3] = -1;
                                    }
                                }
                                else if(bulletDist < 250){
                                    bulletArray[k][0] = 2;
                                }
                                else if(bulletDist < 400){
                                    bulletArray[k][0] = 1;
                                }
                                bulletArray[k][2] += speed5;
                                break;
                            case 5:
                                bulletDist = sqrt(((bulletArray[k][2] - objX6)*(bulletArray[k][2] - objX6))+((bulletArray[k][3] - objY6)*(bulletArray[k][3] - objY6)));
                                if(bulletDist < 100){
                                    bulletArray[k][0] = 3;
                                    touchDist = sqrt(((*(pos+1) - objX6)*(*(pos+1) - objX6))+((*(pos+2) - objY6)*(*(pos+2) - objY6)));
                                    if(touchDist < 60 && bulletDist < 60 && *pos != 0){
                                        score++;
                                        printf("Score : %d", score);
                                        bulletArray[k][0] = -1;
                                        bulletArray[k][1] = -1;
                                        bulletArray[k][2] = -1;
                                        bulletArray[k][3] = -1;
                                    }
                                }
                                else if(bulletDist < 250){
                                    bulletArray[k][0] = 2;
                                }
                                else if(bulletDist < 400){
                                    bulletArray[k][0] = 1;
                                }
                                bulletArray[k][2] += speed6;
                                bulletArray[k][3] += speed6/2;
                                break;
                            default:
                                break;
                        }
                        switch(bulletArray[k][0]){
                            case 1 : AddImage(&bullet0, bulletArray[k][2], bulletArray[k][3]); break;
                            case 2 : AddImage(&bullet1, bulletArray[k][2], bulletArray[k][3]); break;
                            case 3 : AddImage(&bullet2, bulletArray[k][2], bulletArray[k][3]); break;
                            default : break;
                        }
                        if(bulletArray[k][2] > 800 || bulletArray[k][2] < 0 || bulletArray[k][3] > 480 || bulletArray[k][3] < 0){
                            printf("Begone\n");
                            bulletArray[k][0] = -1;
                            bulletArray[k][1] = -1;
                            bulletArray[k][2] = -1;
                            bulletArray[k][3] = -1;
                        }                        
                    }
                }                

                if (countCow == 0)
                    AddImage(&cow0, 350, 325);
                else if (countCow == 1)
                    AddImage(&cow1, 350, 325);            
                else if (countCow == 2)
                    AddImage(&cow2, 350, 325);            
                else if (countCow == 3)
                    AddImage(&cow3, 350, 325);            
                else if (countCow == 4)
                    AddImage(&cow4, 350, 325);            
                else if (countCow == 5)
                    AddImage(&cow5, 350, 325);

                AddImage(&circle0, objX1, objY1);
                AddImage(&circle0, objX2, objY2);
                AddImage(&circle0, objX3, objY3);
                AddImage(&circle0, objX4, objY4);
                AddImage(&circle0, objX5, objY5);
                AddImage(&circle0, objX6, objY6);       

                if(count % 2 == 0){
                    if (countCow < 5)
                        countCow++;                
                    else
                        countCow = 0; 
                }                
                count++;           
            }
            if(state == 5){
                AddFullImage(&loadingBg, 0, 0); 
            }
            if(state == 6){
                if(winStatus2==1)
                    AddFullImage(&win, 0, 0); 
                else
                    AddFullImage(&lose, 0, 0);
            }
            myFrameBuffer = 0x20000000;
            previousBuffer = 0x20000000;
            j = 0;
            do
            {             
                i = 0;
                while (i < 800)
                {
                    *previousBuffer++ = ((int)ImageGetPixel(&landscape, i, j, 0)<< 16) + (int)(ImageGetPixel(&landscape, i, j, 1) << 8) + (int)ImageGetPixel(&landscape, i, j, 2);
                    i += 1;
                }
                j += 1;
            } while (j < 480);
            myFrameBuffer = previousBuffer;
            myFrameBuffer = 0x20000000; 
        
        }
        TSKsleep(OS_MS_TO_TICK(20));
    }
}

/*-----------------------------------------------------------*/

void spi_CallbackInterrupt(uint32_t icciar, void *context)
{
    uint32_t status = alt_read_word(SPI_STATUS);
    uint32_t rxdata = alt_read_word(SPI_RXDATA);

    // Do something
    // printf("INFO: IRQ from SPI : %x (status = %x)\r\n", rxdata, status);
    printf("Received : %x\n", alt_read_word(fpga_spi));

    *myFrameBuffer++ = rxdata;
    if (myFrameBuffer >= 0x20000000 + (800 * 480 * 4))
        myFrameBuffer = 0x20000000;
    if(alt_read_word(fpga_spi) == 1 && state == 1){
        if(role == 2){
            songInt = 1;
            isSongSelected = 1;
            alt_write_word(SPI_TXDATA, 1);
        }
    }
    if(alt_read_word(fpga_spi) == 2 && state == 1){
        if(role == 2){
            songInt = 2;
            isSongSelected = 1;
            alt_write_word(SPI_TXDATA, 2);

        }
    }
    if(alt_read_word(fpga_spi) == 4 && state == 3){
        spiNiosReady = 1;
    }
    if((alt_read_word(fpga_spi) == 0x10) && state == 5){
        winStatus = 1;
    }
    else if((alt_read_word(fpga_spi) == 0x20) && state == 5){
        winStatus = 2;
    }
    else if((alt_read_word(fpga_spi) == 0x40) && state == 5){
        winStatus = 3;
    }
    if((alt_read_word(fpga_spi) == 0x10) && state == 6){
        winStatus2 = 1;
    }
    else if((alt_read_word(fpga_spi) == 0x20) && state == 6){
        winStatus2 = 2;
    }
    else if((alt_read_word(fpga_spi) == 0x40) && state == 6){
        winStatus2 = 3;
    }
    // Clear the status of SPI core
    alt_write_word(SPI_STATUS, 0x00);
}

/*-----------------------------------------------------------*/

void mtc2_CallbackInterrupt(uint32_t icciar, void *context)
{
    //printf("INFO: IRQ from MTC2\r\n");

    // Clear the interruptmask of PIO core
    alt_write_word(PIOinterruptmask_fpga_MTL, 0x0);

    mtc2_QueryData(myTouch);

    // Enable the interruptmask and edge register of PIO core for new interrupt
    alt_write_word(PIOinterruptmask_fpga_MTL, 0x1);
    alt_write_word(PIOedgecapture_fpga_MTL, 0x1);
}

/*-----------------------------------------------------------*/

void Task_HPS_Led(void)
{
    MTX_t *PrtMtx; // Mutex for exclusive access to printf()
    MBX_t *PrtMbx;
    intptr_t *PtrMsg;

    setup_hps_gpio(); // This is the Adam&Eve Task and we have first to setup the gpio
    button_ConfigureInterrupt();
    spi_Nios_ConfigureInterrupt();

    PrtMtx = MTXopen("Printf Mtx");
    MTXlock(PrtMtx, -1);
    printf("\n\nDE10-Nano - MyApp_MTL2\n\n");
    printf("Task_HPS_Led running on core #%d\n\n", COREgetID());
    MTXunlock(PrtMtx);

    PrtMbx = MBXopen("MyMailbox", 128);

    for (;;)
    {
        if (MBXget(PrtMbx, PtrMsg, 0) == 0)
        { // 0 = Never blocks
            MTXlock(PrtMtx, -1);
            printf("Receive message (Core = %d)\n", COREgetID());
            MTXunlock(PrtMtx);
        }
        toogle_hps_led();
        TSKsleep(OS_MS_TO_TICK(250));
    }
}

/*-----------------------------------------------------------*/

void Task_FPGA_Led(void)
{
    uint32_t leds_mask;

    alt_write_word(fpga_leds, 0x01);

    for (;;)
    {
        leds_mask = alt_read_word(fpga_leds);
        if (leds_mask != (0x01 << (LED_PIO_DATA_WIDTH - 1)))
        {
            // rotate leds
            leds_mask <<= 1;
        }
        else
        {
            // reset leds
            leds_mask = 0x1;
        }
        alt_write_word(fpga_leds, leds_mask);

        TSKsleep(OS_MS_TO_TICK(250));
    }
}
/*-----------------------------------------------------------*/

void Task_FPGA_Button(void)
{
    MTX_t    *PrtMtx;       // Mutex for exclusive access to printf()
    MBX_t    *PrtMbx;
    intptr_t  PtrMsg;
    SEM_t    *PtrSem;
    uint32_t button_mask;
    int state;
    
    PrtMtx = MTXopen("Printf Mtx");
    PtrSem = SEMopen("MySemaphore");
    
    for( ;; )
    {
        SEMwait(PtrSem, -1);    // -1 = Infinite blocking
        SEMreset(PtrSem);

        button_mask = alt_read_word(fpga_buttons);
        if (button_mask == 0b10){
            state = 1;
        }
        else if(button_mask == 0b01){
            state = 0;
        }
        
    }
}


/*-----------------------------------------------------------*/

void button_CallbackInterrupt (uint32_t icciar, void *context)
{
    SEM_t    *PtrSem;
    MBX_t    *PrtMbx;
    PrtMbx = MBXopen("bulletMailbox", 128);
    
    // Clear the interruptmask of PIO core
    alt_write_word(fpga_buttons + (2*4), 0x0);
    // Enable the interruptmask and edge register of PIO core for new interrupt
    alt_write_word(fpga_buttons + (2*4), 0x3);
    alt_write_word(fpga_buttons + (3*4), 0x3);
    printf("Hey there\n");  
    PtrSem = SEMopen("MySemaphore");
    SEMpost(PtrSem);
    if(alt_read_word(fpga_buttons) == 0b10){
        PtrSem = SEMopen("MySem_DMA");
        SEMpost(PtrSem);
    }
    else{
        MBXput(PrtMbx, 1, -1);  
    }
}

/*-----------------------------------------------------------*/

void button_ConfigureInterrupt( void )
{
    OSisrInstall(GPT_BUTTON_IRQ, (void *) &button_CallbackInterrupt);
    GICenable(GPT_BUTTON_IRQ, 128, 1);
    
    // Enable interruptmask and edgecapture of PIO core for buttons 0 and 1
    alt_write_word(fpga_buttons + (2*4), 0x3);
    alt_write_word(fpga_buttons + (3*4), 0x3);
}

/*-----------------------------------------------------------*/

void Task_SPI(void)
{
    alt_write_word(fpga_leds, 0x08);
    MTX_t    *PrtMtx;       // Mutex for exclusive access to printf()
    MBX_t    *PrtMbx;
    intptr_t  *PtrMsg;
    SEM_t    *PtrSem;
    int test;
    int spi_val[4];
    int state;
    
    PrtMtx = MTXopen("Printf Mtx");
    PrtMbx = MBXopen("beatMailbox", 128);
    PtrSem = SEMopen("spiSemaphore");
    
    for( ;; )
    {
        SEMwait(PtrSem, -1);    // Recieve SPI message
        SEMreset(PtrSem);  
           
        if (MBXget(PrtMbx, PtrMsg, 0) == 0) { // Check if buttons message            
            printf("NIOS : %d\n", *PtrMsg); 
            if(*PtrMsg == 2){
                gameFinished = 1; 
                printf("Done\n"); 

            }
            
        }
          
        //TSKsleep(OS_MS_TO_TICK(100));
    }
}

/*-----------------------------------------------------------*/

void spi_Nios_ConfigureInterrupt( void )
{
    OSisrInstall(GPT_SPI_NIOS_IRQ, (void *) &spi_Nios_CallbackInterrupt);
    GICenable(GPT_SPI_NIOS_IRQ, 128, 1);
    // Initialize register values
    alt_write_word(spi_rx + (2*4), 0x0);
    alt_write_word(spi_rx + (3*4), 0x80);
}


/*-----------------------------------------------------------*/

void spi_Nios_CallbackInterrupt(uint32_t icciar, void *context)
{
    SEM_t    *PtrSem; 
    MBX_t    *PrtMbx; 
    MBX_t    *PrtMbx2; 

    int value = alt_read_word(spi_rx);
    // Reset register values  
    alt_write_word(spi_rx + (2*4), 0x0);
    alt_write_word(spi_rx + (3*4), 0x80);
    if(niosFinished == 0){
        niosFinished = 1;
    }
    PrtMbx = MBXopen("bulletMailbox",  128);
    PrtMbx2 = MBXopen("beatMailbox", 128);
    // if(count < 3)
    //     count++;    
    // else
    //     count = 0;    
    
    MBXput(PrtMbx, 1, -1); 
    MBXput(PrtMbx, 1, -1);
    MBXput(PrtMbx2, (intptr_t) value, -1);        
    PtrSem = SEMopen("spiSemaphore");
    SEMpost(PtrSem);
    
    
    
    
    
}

/*-----------------------------------------------------------*/

void setup_hps_gpio()
{
    uint32_t hps_gpio_config_len = 2;
    ALT_GPIO_CONFIG_RECORD_t hps_gpio_config[] = {
        {HPS_LED_IDX, ALT_GPIO_PIN_OUTPUT, 0, 0, ALT_GPIO_PIN_DEBOUNCE, ALT_GPIO_PIN_DATAZERO},
        {HPS_KEY_N_IDX, ALT_GPIO_PIN_INPUT, 0, 0, ALT_GPIO_PIN_DEBOUNCE, ALT_GPIO_PIN_DATAZERO}};

    assert(ALT_E_SUCCESS == alt_gpio_init());
    assert(ALT_E_SUCCESS == alt_gpio_group_config(hps_gpio_config, hps_gpio_config_len));
}

/*-----------------------------------------------------------*/

void toogle_hps_led()
{
    uint32_t hps_led_value = alt_read_word(ALT_GPIO1_SWPORTA_DR_ADDR);
    hps_led_value >>= HPS_LED_PORT_BIT;
    hps_led_value = !hps_led_value;
    hps_led_value <<= HPS_LED_PORT_BIT;
    alt_gpio_port_data_write(HPS_LED_PORT, HPS_LED_MASK, hps_led_value);
}

/*-----------------------------------------------------------*/

void cmd_ls()
{
    DIR_t *Dinfo;
    struct dirent *DirFile;
    struct stat Finfo;
    char Fname[SYS_CALL_MAX_PATH + 1];
    char MyDir[SYS_CALL_MAX_PATH + 1];
    struct tm *Time;

    /* Refresh the current directory path            */
    if (NULL == getcwd(&MyDir[0], sizeof(MyDir)))
    {
        printf("ERROR: cannot obtain current directory\n");
        return;
    }

    /* Open the dir to see if it's there            */
    if (NULL == (Dinfo = opendir(&MyDir[0])))
    {
        printf("ERROR: cannot open directory\n");
        return;
    }

    /* Valid directory, read each entries and print    */
    while (NULL != (DirFile = readdir(Dinfo)))
    {
        strcpy(&Fname[0], &MyDir[0]);
        strcat(&Fname[0], "/");
        strcat(&Fname[0], &(DirFile->d_name[0]));

        stat(&Fname[0], &Finfo);
        putchar(((Finfo.st_mode & S_IFMT) == S_IFLNK) ? 'l' : ((Finfo.st_mode & S_IFMT) == S_IFDIR) ? 'd' : ' ');
        putchar((Finfo.st_mode & S_IRUSR) ? 'r' : '-');
        putchar((Finfo.st_mode & S_IWUSR) ? 'w' : '-');
        putchar((Finfo.st_mode & S_IXUSR) ? 'x' : '-');

        if ((Finfo.st_mode & S_IFLNK) == S_IFLNK)
        {
            printf(" (%-9s mnt point)           - ", media_info(DirFile->d_drv));
        }
        else
        {
            Time = localtime(&Finfo.st_mtime);
            if (Time != NULL)
            {
                printf(" (%04d.%02d.%02d %02d:%02d:%02d) ", Time->tm_year + 1900,
                       Time->tm_mon,
                       Time->tm_mday,
                       Time->tm_hour,
                       Time->tm_min,
                       Time->tm_sec);
            }
            printf(" %10lu ", Finfo.st_size);
        }
        puts(DirFile->d_name);
    }
    closedir(Dinfo);
}

/*-----------------------------------------------------------*/

/* die gracelessly */

static void
die(char *message)
{
    fprintf(stderr, "ppm: %s\n", message);
    exit(1);
}

/* check a dimension (width or height) from the image file for reasonability */

static void
checkDimension(int dim)
{
    if (dim < 1 || dim > 4000)
        die("file contained unreasonable width or height");
}

/* read a header: verify format and get width and height */

static void
readPPMHeader(FILE *fp, int *width, int *height)
{
    char ch;
    int maxval;

    if (fscanf(fp, "P%c\n", &ch) != 1 || ch != '6')
        die("file is not in ppm raw format; cannot read");

    /* skip comments */
    ch = getc(fp);
    while (ch == '#')
    {
        do
        {
            ch = getc(fp);
        } while (ch != '\n'); /* read to the end of the line */
        ch = getc(fp);        /* thanks, Elliot */
    }

    if (!isdigit(ch))
        die("cannot read header information from ppm file");

    ungetc(ch, fp); /* put that digit back */

    /* read the width, height, and maximum value for a pixel */
    fscanf(fp, "%d%d%d\n", width, height, &maxval);

    if (maxval != 255)
        die("image is not true-color (24 bit); read failed");

    checkDimension(*width);
    checkDimension(*height);
}

/************************ exported functions ****************************/

Image *
ImageCreate(int width, int height)
{
    Image *image = (Image *)malloc(sizeof(Image));

    if (!image)
        die("cannot allocate memory for new image");

    image->width = width;
    image->height = height;
    image->data = (u_char *)malloc(width * height * 3);

    if (!image->data)
        die("cannot allocate memory for new image");

    return image;
}

Image *
ImageRead(char *filename)
{
    int width, height, num, size;
    u_char *p;

    Image *image = (Image *)malloc(sizeof(Image));
    FILE *fp = fopen(filename, "r");

    if (!image)
        die("cannot allocate memory for new image");
    if (!fp)
        die("cannot open file for reading");

    readPPMHeader(fp, &width, &height);

    size = width * height * 3;
    image->data = (u_char *)malloc(size);
    image->width = width;
    image->height = height;

    if (!image->data)
        die("cannot allocate memory for new image");

    num = fread((void *)image->data, 1, (size_t)size, fp);

    if (num != size)
        die("cannot read image data from file");

    fclose(fp);
    puts("Image ready");
    return image;
}

void ImageWrite(Image *image, char *filename)
{
    int num;
    int size = image->width * image->height * 3;

    FILE *fp = fopen(filename, "w");

    if (!fp)
        die("cannot open file for writing");

    fprintf(fp, "P6\n%d %d\n%d\n", image->width, image->height, 255);

    num = fwrite((void *)image->data, 1, (size_t)size, fp);

    if (num != size)
        die("cannot write image data to file");

    fclose(fp);
}

int ImageWidth(Image *image)
{
    return image->width;
}

int ImageHeight(Image *image)
{
    return image->height;
}

void ImageClear(Image *image, u_char red, u_char green, u_char blue)
{
    int i;
    int pix = image->width * image->height;

    u_char *data = image->data;

    for (i = 0; i < pix; i++)
    {
        *data++ = red;
        *data++ = green;
        *data++ = blue;
    }
}

void ImageSetPixel(Image *image, int x, int y, int chan, u_char val)
{
    int offset = (y * image->width + x) * 3 + chan;

    image->data[offset] = val;
}

u_char
ImageGetPixel(Image *image, int x, int y, int chan)
{
    int offset = (y * image->width + x) * 3 + chan;

    return image->data[offset];
}

void AddImage(Image *image, int x, int y){
    int w = image->width;
    int h = image->height;
    int r;
    int g;
    int b;    
    for(int i = 0; i<h; i++){
        for(int j = 0; j<w; j++){
            r = ImageGetPixel(image, j, i, 0);
            g = ImageGetPixel(image, j, i, 1);
            b = ImageGetPixel(image, j, i, 1);
            if(!(r == 0 && g == 0 && b == 0) && !(x+j<0 || x+j>800 || y+i < 0 || y+i > 480)){
                ImageSetPixel(&landscape, x+j, y+i, 0, r);
                ImageSetPixel(&landscape, x+j, y+i, 1, g);
                ImageSetPixel(&landscape, x+j, y+i, 2, b);
            }
        }
    }
}

void AddFullImage(Image *image, int x, int y){
    int w = image->width;
    int h = image->height;       
    for(int i = 0; i<h; i++){
        for(int j = 0; j<w; j++){            
            ImageSetPixel(&landscape, x+j, y+i, 0, ImageGetPixel(image, j, i, 0));
            ImageSetPixel(&landscape, x+j, y+i, 1, ImageGetPixel(image, j, i, 1));
            ImageSetPixel(&landscape, x+j, y+i, 2, ImageGetPixel(image, j, i, 2));            
        }
    }
}





