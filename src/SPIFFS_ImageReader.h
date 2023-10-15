/*!
 * @file SPIFFS_ImageReader.h
 *
 * This is based on Adafruit's ImageReader library for Arduino, designed to
 * work with Adafruit_GFX plus a display device-specific library.
 *
 * Original library written by Phil "PaintYourDragon" Burgess for Adafruit Industries.
 * SPIFFS version written by Luca Dentella.
 *
 * BSD license, all text here must be included in any redistribution.
 */

#ifndef __SPIFFS_IMAGE_READER_H__
#define __SPIFFS_IMAGE_READER_H__

/* 
 * can load images up to NUM_CANVAS * CANVAS_HEIGHT pixels in height. Example:
 * #define NUM_CANVAS 12
 * #define CANVAS_HEIGHT 20
 * --> max image height = 12*20 = 240px
 */
#define NUM_CANVAS 12
#define CANVAS_HEIGHT 20

#include "SPIFFS.h"
#include "Adafruit_SPITFT.h"

// This set of guards bluntly solves an annoying compilation error when this is used alongside the Adafruit version.
// So long as this is loaded after the Adafruit version, it won't try and define these types again.
#ifndef __ADAFRUIT_IMAGE_READER_H__
/** Status codes returned by drawBMP() and loadBMP() */
enum ImageReturnCode
{
  IMAGE_SUCCESS,            // Successful load (or image clipped off screen)
  IMAGE_ERR_FILE_NOT_FOUND, // Could not open file
  IMAGE_ERR_FORMAT,         // Not a supported image format
  IMAGE_ERR_MALLOC          // Could not allocate image (loadBMP() only)
};

/** Image formats returned by loadBMP() */
enum ImageFormat
{
  IMAGE_NONE, // No image was loaded; IMAGE_ERR_* condition
  IMAGE_16    // GFXcanvas16 image (SUPPORTED)
};
#endif
/*!
   @brief  Data bundle returned with an image loaded to RAM. Used by
           ImageReader.loadBMP() and Image.draw(), not ImageReader.drawBMP().
*/
class SPIFFS_Image
{
public:
  SPIFFS_Image(void);
  ~SPIFFS_Image(void);
  int16_t width(void) const;  // Return image width in pixels
  int16_t height(void) const; // Return image height in pixels
  void draw(Adafruit_SPITFT &tft, int16_t x, int16_t y);
  /*!
      @brief   Return canvas image format.
      @return  An ImageFormat type: IMAGE_1 for a GFXcanvas1, IMAGE_8 for
               a GFXcanvas8, IMAGE_16 for a GFXcanvas16, IMAGE_NONE if no
               canvas currently allocated.
  */
  ImageFormat getFormat(void) const { return (ImageFormat)format; }

protected:
  uint16_t w, h;
  GFXcanvas16 *canvas[NUM_CANVAS]; // Canvas object if 16bpp;
  uint8_t format;                  ///< Canvas bundle type in use
  void dealloc(void);              ///< Free/deinitialize variables
  friend class SPIFFS_ImageReader; ///< Loading occurs here
};

/*!
   @brief  An optional adjunct to Adafruit_SPITFT that reads RGB BMP
           images (maybe others in the future) from a flash filesystem
           (SD card or SPI/QSPI flash). It's purposefully been made an
           entirely separate class (rather than part of SPITFT or GFX
           classes) so that Arduino code that uses GFX or SPITFT *without*
           image loading does not need to incur the RAM overhead and
           additional dependencies of the Adafruit_SPIFlash library by
           its mere inclusion. The syntaxes can therefore be a bit
           bizarre (passing display object as an argument), see examples
           for use.
*/
class SPIFFS_ImageReader
{
public:
  SPIFFS_ImageReader();
  ~SPIFFS_ImageReader(void);
  ImageReturnCode loadBMP(char *filename, SPIFFS_Image &img);
  ImageReturnCode bmpDimensions(char *filename, int32_t *w, int32_t *h);
  void printStatus(ImageReturnCode stat, Stream &stream = Serial);

protected:
  File file; ///< Current Open file
  ImageReturnCode coreBMP(char *filename, SPIFFS_Image *img);
  uint16_t readLE16(void);
  uint32_t readLE32(void);
};

#endif // __SPIFFS_IMAGE_READER_H__
