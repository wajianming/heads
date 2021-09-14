#define _DEFAULT_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h> 
#include <sys/mman.h>

#include <signal.h>
#include <pthread.h>

/*--------------------------------------------------------------------------}
{                        BITMAP FILE HEADER DEFINITION                      }
{--------------------------------------------------------------------------*/
#pragma pack(1)									// Must be packed as read from file
typedef struct tagBITMAPFILEHEADER {
	uint16_t  bfType; 							// Bitmap type should be "BM"
	uint32_t  bfSize; 							// Bitmap size in bytes
	uint16_t  bfReserved1; 						// reserved short1
	uint16_t  bfReserved2; 						// reserved short2
	uint32_t  bfOffBits; 						// Offset to bmp data
} BITMAPFILEHEADER, * LPBITMAPFILEHEADER, * PBITMAPFILEHEADER;
#pragma pack()

/*--------------------------------------------------------------------------}
{                    BITMAP FILE INFO HEADER DEFINITION						}
{--------------------------------------------------------------------------*/
#pragma pack(1)									// Must be packed as read from file
typedef struct tagBITMAPINFOHEADER {
	uint32_t biSize; 							// Bitmap file size
	uint32_t biWidth; 							// Bitmap width
	uint32_t biHeight;							// Bitmap height
	uint16_t biPlanes; 							// Planes in image
	uint16_t biBitCount; 						// Bits per byte
	uint32_t biCompression; 					// Compression technology
	uint32_t biSizeImage; 						// Image byte size
	uint32_t biXPelsPerMeter; 					// Pixels per x meter
	uint32_t biYPelsPerMeter; 					// Pixels per y meter
	uint32_t biClrUsed; 						// Number of color indexes needed
	uint32_t biClrImportant; 					// Min colours needed
} BITMAPINFOHEADER, * PBITMAPINFOHEADER;
#pragma pack()

/*--------------------------------------------------------------------------}
{				  BITMAP VER 4 FILE INFO HEADER DEFINITION					}
{--------------------------------------------------------------------------*/
#pragma pack(1)
typedef struct tagWIN4XBITMAPINFOHEADER
{
	uint32_t RedMask;       /* Mask identifying bits of red component */
	uint32_t GreenMask;     /* Mask identifying bits of green component */
	uint32_t BlueMask;      /* Mask identifying bits of blue component */
	uint32_t AlphaMask;     /* Mask identifying bits of alpha component */
	uint32_t CSType;        /* Color space type */
	uint32_t RedX;          /* X coordinate of red endpoint */
	uint32_t RedY;          /* Y coordinate of red endpoint */
	uint32_t RedZ;          /* Z coordinate of red endpoint */
	uint32_t GreenX;        /* X coordinate of green endpoint */
	uint32_t GreenY;        /* Y coordinate of green endpoint */
	uint32_t GreenZ;        /* Z coordinate of green endpoint */
	uint32_t BlueX;         /* X coordinate of blue endpoint */
	uint32_t BlueY;         /* Y coordinate of blue endpoint */
	uint32_t BlueZ;         /* Z coordinate of blue endpoint */
	uint32_t GammaRed;      /* Gamma red coordinate scale value */
	uint32_t GammaGreen;    /* Gamma green coordinate scale value */
	uint32_t GammaBlue;     /* Gamma blue coordinate scale value */
} WIN4XBITMAPINFOHEADER;
#pragma pack()

// 'global' variables to store screen info
static volatile bool daemonlized = false;
uint8_t* fbp = 0;
struct fb_var_screeninfo vinfo = { 0 };
struct fb_fix_screeninfo finfo = { 0 };

void intHandler(int dummy) {
    daemonlized = false;
    printf("Interrupt detected.\n");
}

void Draw_Bitmap (FILE* fb,  uint8_t* FrameBuffer)
{
  long int location = 0;
  int x = 0;
	unsigned int fpos = 0;											// File position
  if (fb > 0)													// File opened successfully
  {
    size_t br;
    BITMAPFILEHEADER bmpHeader;
    fseek(fb, SEEK_SET, 0);
    br = fread(&bmpHeader, 1, sizeof(bmpHeader), fb);		// Read the bitmap header 
    if (br == sizeof(bmpHeader) && bmpHeader.bfType == 0x4D42) // Check it is BMP file
    {
      BITMAPINFOHEADER bmpInfo;
      fpos = sizeof(bmpHeader);							// File position is at sizeof(bmpHeader)
      br = fread(&bmpInfo, 1, sizeof(bmpInfo), fb);		// Read the bitmap info header
      if (br == sizeof(bmpInfo))							// Check bitmap info read worked
      {
        uint32_t xferWidth, p2width;
        fpos += sizeof(bmpInfo);						// File position moved sizeof(bmpInfo)
        if (bmpInfo.biSize == 108)
        {
          printf("bitmap v4\n");
          WIN4XBITMAPINFOHEADER bmpInfo4x;
          br = fread(&bmpInfo4x, 1, sizeof(bmpInfo4x), fb); // Read the bitmap v4 info header exta data
          fpos += sizeof(bmpInfo4x);					// File position moved sizeof(bmpInfo4x)
        }
        if (bmpHeader.bfOffBits > fpos)
        {
          uint8_t b;
          for (int i = 0; i < bmpHeader.bfOffBits - fpos; i++)
            fread(&b, 1, sizeof(b), fb);
          fpos = bmpHeader.bfOffBits;
        }
        switch (bmpInfo.biBitCount)
        {
          case 1:
            xferWidth = bmpInfo.biWidth / 8;
            break;
          case 2:
            xferWidth = bmpInfo.biWidth / 4;
            break;
          case 4:
            xferWidth = bmpInfo.biWidth / 2;
            break;
          case 8:
            xferWidth = bmpInfo.biWidth;
            break;
          case 16:
            xferWidth = bmpInfo.biWidth * 2;
            break;
          case 24:
            xferWidth = bmpInfo.biWidth * 3;
            break;
          default:
            xferWidth = bmpInfo.biWidth * 4;
            break;
        }
        p2width = xferWidth / 4;							// Divid the xfer width by 4
        if (p2width * 4 < xferWidth) p2width++;			// Not divisable so add one to p2width
        p2width = p2width * 4;							// p2width is xfer width divisible by 4
        /* file positioned at image we must transfer it to screen */
        int y = 0;
        while (!feof(fb) && y < bmpInfo.biHeight)	// While no error and not all line read
        {
          fread(&FrameBuffer[(bmpInfo.biHeight-1-y) * finfo.line_length], 1, p2width, fb);
          y++;
        }
        if (bmpInfo.biBitCount != 32){
          printf("BMP Res=%dx%d, Bit count = %d\n", bmpInfo.biWidth, bmpInfo.biHeight, bmpInfo.biBitCount);
          perror("Error: Mismatch bitcount\n");
        }
      }
		}
	}
}

void Draw_Bitmap2 (uint8_t* FrameBuffer)
{
  long int location = 0;
  int x = 0, y = 0;
  for( y=0; y<81; y++)
    for( x=0; x<vinfo.xres; x++){
      location = (x+vinfo.xoffset) * (vinfo.bits_per_pixel/8) +
        (y+vinfo.yoffset) * finfo.line_length;
      if(vinfo.bits_per_pixel ==32) {
        *(FrameBuffer + location) = 255;
        *(FrameBuffer + location + 1) = 255;
        *(FrameBuffer + location + 2) = 255;
        *(FrameBuffer + location + 3) = 0;
      }
    }
}
// application entry point
int main(int argc, char* argv[])
{
  int cmd_opt = 0;
  char* fdev = NULL;
  char* banner_image = NULL;

  signal(SIGINT, intHandler);
  if (cmd_opt == '?') {
    printf("option: -%c\n\t"
      "f: framebuffer device\n\t"
      "b: banner image file. (32bits BMP format)\n\t"
      "d: daemonlize", cmd_opt);
    return(0);
  }
  while ((cmd_opt = getopt(argc, argv, "f:b:d")) != -1) {
    switch (cmd_opt) {
      case 'f':
        fdev = optarg;
        break;
      case 'b':
        banner_image = optarg;
        break;
      case 'd':
        daemonlized = true;
        break;
      case '?':
        printf("option: -%c\n\t"
          "f: framebuffer device\n\t"
          "b: banner image file. (32bits BMP format)\n\t"
          "d: daemonlize", cmd_opt);
        return(0);
    }
  }

  if (fdev == NULL || banner_image == NULL) {
		perror("Error: framebuffer and banner_image is mandatory.\n");
    return(1);
  }

	// Open the file for reading and writing
	int fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd == -1) {
		perror("Error: cannot open framebuffer device.\n");
		return(1);
	}

	// Get variable screen information
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
		perror("Error reading variable information.\n");
		close(fbfd);
		return(1);
	}
  printf("Res=%dx%d, bpp=%d\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

	// Change variable info - force 800x600x24 bit
  /*
	vinfo.bits_per_pixel = 24;
	vinfo.xres = 1920;
	vinfo.yres = 1080;
	if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
		perror("Error setting variable information.\n");
		close(fbfd);
		return(1);
	}
  */
	// Get fixed screen information
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
		perror("Error reading fixed information.\n");
		close(fbfd);
		return(1);
	}

	// map fb to user mem 
	fbp = (uint8_t*)mmap(0,
		finfo.smem_len,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		fbfd,
		0);

	FILE* fb = fopen(banner_image, "rb");							// Open the bitmap file
  const unsigned int refresh_time = 1000000 / 50;
  int counter = 3;
	if ((int)fbp != -1 && fb > 0) {
    do {
		  Draw_Bitmap2(fbp);
		  Draw_Bitmap(fb, fbp);
      usleep(refresh_time);
    } while (daemonlized);
	}
	else perror("Header open failed");
	fclose(fb);

	// cleanup

	// unmap fb file from memory
	munmap(fbp, finfo.smem_len);
	// close fb file    
	close(fbfd);

	return 0;
}
