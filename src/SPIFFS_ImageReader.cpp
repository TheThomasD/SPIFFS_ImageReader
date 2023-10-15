/*!
 * @file SPIFFS_ImageReader.cpp
 *
 * @mainpage Companion library for Adafruit_GFX to load images from SPIFFS partition.
 *           Load-to-display and load-to-RAM are supported.
 *
 * @section intro_sec Introduction
 *
 * This is based on Adafruit's ImageReader library for the
 * Arduino platform. It is designed to work in conjunction with Adafruit_GFX
 * and a display-specific library (e.g. Adafruit_ILI9341).
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * @section dependencies Dependencies
 *
 * This library depends on
 * <a href="https://github.com/adafruit/Adafruit_GFX">Adafruit_GFX</a>
 * plus a display device-specific library such as
 * <a href="https://github.com/adafruit/Adafruit_ILI9341">Adafruit_ILI9341</a>
 * or other subclasses of SPITFT. Filesystem reading is handled through the SPIFFS library
 * included in Arduino core for the ESP32.
 * Please make sure you have installed the latest versions before
 * using this library.
 *
 * @section author Author
 *
 * Written by Luca Dentella, based on original work by Phil "PaintYourDragon" Burgess for Adafruit Industries.
 *
 * @section license License
 *
 * BSD license, all text here must be included in any redistribution.
 */

#include "SPIFFS_ImageReader.h"

// Buffers in BMP draw function (to screen) require 5 bytes/pixel: 3 bytes
// for each BMP pixel (R+G+B), 2 bytes for each TFT pixel (565 color).
// Buffers in BMP load (to canvas) require 3 bytes/pixel (R+G+B from BMP),
// no interim 16-bit buffer as data goes straight to the canvas buffer.
// Because buffers are flushed at the end of each scanline (to allow for
// cropping, vertical flip, scanline padding, etc.), no point in any of
// these pixel counts being more than the screen width.
// (Maybe to do: make non-AVR loader dynamically allocate buffer based
// on screen or image size.)

#ifdef __AVR__
#define BUFPIXELS 24 ///<  24 * 5 =  120 bytes
#else
#define BUFPIXELS 200 ///< 200 * 5 = 1000 bytes
#endif

// SPIFFS_Image CLASS ****************************************************
// This has been created as a class here rather than in Adafruit_GFX because
// it's a new type returned specifically by the SPIFFS_ImageReader class
// and needs certain flexibility not present in the latter's GFXcanvas*
// classes (having been designed for flash-resident bitmaps).

/*!
    @brief   Constructor.
    @return  'Empty' SPIFFS_Image object.
*/
SPIFFS_Image::SPIFFS_Image(void)
    : format(IMAGE_NONE)
{
  for (int i = 0; i < NUM_CANVAS; i++)
    canvas[i] = NULL;
}

/*!
    @brief   Destructor.
    @return  None (void).
*/
SPIFFS_Image::~SPIFFS_Image(void) { dealloc(); }

/*!
    @brief   Deallocates memory associated with SPIFFS_Image object
             and resets member variables to 'empty' state.
    @return  None (void).
*/
void SPIFFS_Image::dealloc(void)
{
  for (int i = 0; i < NUM_CANVAS; i++)
  {
    if (canvas[i] != NULL)
    {
      delete canvas[i];
      canvas[i] = NULL;
    }
  }
  format = IMAGE_NONE;
}

/*!
    @brief   Get width of SPIFFS_Image object.
    @return  Width in pixels, or 0 if no image loaded.
*/
int16_t SPIFFS_Image::width(void) const
{
  if (format != IMAGE_NONE)
  { // Image allocated?
    if (format == IMAGE_16)
      return w;
  }
  return 0;
}

/*!
    @brief   Get height of SPIFFS_Image object.
    @return  Height in pixels, or 0 if no image loaded.
*/
int16_t SPIFFS_Image::height(void) const
{
  if (format != IMAGE_NONE)
  { // Image allocated?
    if (format == IMAGE_16)
      return h;
  }
  return 0;
}

/*!
    @brief   Draw image to an Adafruit_SPITFT-type display.
    @param   tft
             Screen to draw to (any Adafruit_SPITFT-derived class).
    @param   x
             Horizontal offset in pixels; left edge = 0, positive = right.
             Value is signed, image will be clipped if all or part is off
             the screen edges. Screen rotation setting is observed.
    @param   y
             Vertical offset in pixels; top edge = 0, positive = down.
    @return  None (void).
*/
void SPIFFS_Image::draw(Adafruit_SPITFT &tft, int16_t x, int16_t y)
{
  if (format == IMAGE_16)
  {
    for (int i = 0; i < NUM_CANVAS; i++)
    {
      if (canvas[i] != NULL)
      {
        tft.drawRGBBitmap(x, y, canvas[i]->getBuffer(),
                          canvas[i]->width(), canvas[i]->height());
        y += CANVAS_HEIGHT;
      }
    }
  }
}

// SPIFFS_ImageReader CLASS **********************************************
// Loads images from SD card to screen or RAM.

/*!
    @brief   Constructor.
    @return  SPIFFS_ImageReader object.
    @param   fs
             FAT filesystem associated with this SPIFFS_ImageReader
             instance. Any images to load will come from this filesystem;
             if multiple filesystems are required, each will require its
             own SPIFFS_ImageReader object. The filesystem does NOT need
             to be initialized yet when passed in here (since this will
             often be in pre-setup() declaration, but DOES need initializing
             before any of the image loading or size functions are called!
*/
SPIFFS_ImageReader::SPIFFS_ImageReader() {}

/*!
    @brief   Destructor.
    @return  None (void).
*/
SPIFFS_ImageReader::~SPIFFS_ImageReader(void)
{
  if (file)
    file.close();
  // filesystem is left as-is
}

/*!
    @brief   Loads BMP image file from SD card into RAM (as one of the GFX
             canvas object types) for use with the bitmap-drawing functions.
             Not practical for most AVR microcontrollers, but some of the
             more capable 32-bit micros can afford some RAM for this.
    @param   filename
             Name of BMP image file to load.
    @param   img
             SPIFFS_Image object, contents will be initialized, allocated
             and loaded on success (else cleared).
    @return  One of the ImageReturnCode values (IMAGE_SUCCESS on successful
             completion, other values on failure).
*/
ImageReturnCode SPIFFS_ImageReader::loadBMP(char *filename,
                                            SPIFFS_Image &img)
{
  // Call core BMP-reading function. TFT and working buffer are NULL
  // (unused and allocated in function, respectively), X & Y position are
  // always 0 because full image is loaded (RAM permitting). SPIFFS_Image
  // argument is passed through, and SPI transactions are not needed when
  // loading to RAM (bus is not shared during load).
  return coreBMP(filename, &img);
}

/*!
    @brief   BMP-reading function common both to the draw function (to TFT)
             and load function (to canvas object in RAM). BMP code has been
             centralized here so if/when more BMP format variants are added
             in the future, it doesn't need to be implemented, debugged and
             kept in sync in two places.
    @param   filename
             Name of BMP image file to load.
    @param   tft
             Pointer to TFT object, if loading to screen, else NULL.
    @param   dest
             Working buffer for loading 16-bit TFT pixel data, if loading to
             screen, else NULL.
    @param   x
             Horizontal offset in pixels (if loading to screen).
    @param   y
             Vertical offset in pixels (if loading to screen).
    @param   img
             Pointer to SPIFFS_Image object, if loading to RAM (or NULL
             if loading to screen).
    @param   transact
             Use SPI transactions; 'true' is needed only if loading to screen
             and it's on the same SPI bus as the SD card. Other situations
             can use 'false'.
    @return  One of the ImageReturnCode values (IMAGE_SUCCESS on successful
             completion, other values on failure).
*/
ImageReturnCode SPIFFS_ImageReader::coreBMP(
    char *filename, // SD file to load
    SPIFFS_Image *img)
{

  ImageReturnCode status = IMAGE_ERR_FORMAT; // IMAGE_SUCCESS on valid file
  uint32_t offset;                           // Start of image data in file
  uint32_t headerSize;                       // Indicates BMP version
  int bmpWidth, bmpHeight;                   // BMP width & height in pixels
  uint8_t planes;                            // BMP planes
  uint8_t depth;                             // BMP bit depth
  uint32_t compression = 0;                  // BMP compression mode
  uint32_t colors = 0;                       // Number of colors in palette
  uint32_t rowSize;                          // >bmpWidth if scanline padding
  uint8_t sdbuf[3 * BUFPIXELS];              // BMP read buf (R+G+B/pixel)
#if ((3 * BUFPIXELS) <= 255)
  uint8_t srcidx = sizeof sdbuf; // Current position in sdbuf
#else
  uint16_t srcidx = sizeof sdbuf;
#endif
  uint32_t destidx = 0;
  boolean flip = true;       // BMP is stored bottom-to-top
  uint32_t bmpPos = 0;       // Next pixel position in file
  int loadWidth, loadHeight, // Region being loaded (clipped)
      loadX, loadY;          // "
  int row, col;              // Current pixel pos.
  uint8_t r, g, b;           // Current pixel color
  uint8_t bitIn = 0;         // Bit number for 1-bit data in
  uint8_t bitOut = 0;        // Column mask for 1-bit data out

  // If an SPIFFS_Image object is passed and currently contains anything,
  // free its contents as it's about to be overwritten with new stuff.
  img->dealloc();

  // Open requested file on SD card
  if (!(file = SPIFFS.open(filename, FILE_READ)))
  {
    return IMAGE_ERR_FILE_NOT_FOUND;
  }

  // Parse BMP header. 0x4D42 (ASCII 'BM') is the Windows BMP signature.
  // There are other values possible in a .BMP file but these are super
  // esoteric (e.g. OS/2 struct bitmap array) and NOT supported here!
  if (readLE16() == 0x4D42)
  {                      // BMP signature
    (void)readLE32();    // Read & ignore file size
    (void)readLE32();    // Read & ignore creator bytes
    offset = readLE32(); // Start of image data
    // Read DIB header
    headerSize = readLE32();
    bmpWidth = readLE32();
    bmpHeight = readLE32();
    // If bmpHeight is negative, image is in top-down order.
    // This is not canon but has been observed in the wild.
    if (bmpHeight < 0)
    {
      bmpHeight = -bmpHeight;
      flip = false;
    }
    planes = readLE16();
    depth = readLE16(); // Bits per pixel
    // Compression mode is present in later BMP versions (default = none)
    if (headerSize > 12)
    {
      compression = readLE32();
      (void)readLE32();    // Raw bitmap data size; ignore
      (void)readLE32();    // Horizontal resolution, ignore
      (void)readLE32();    // Vertical resolution, ignore
      colors = readLE32(); // Number of colors in palette, or 0 for 2^depth
      (void)readLE32();    // Number of colors used (ignore)
      // File position should now be at start of palette (if present)
    }
    if (!colors)
      colors = 1 << depth;

    loadWidth = bmpWidth;
    loadHeight = bmpHeight;
    loadX = 0;
    loadY = 0;
    if ((planes == 1) && (compression == 0))
    { // Only uncompressed is handled

      // BMP rows are padded (if needed) to 4-byte boundary
      rowSize = ((depth * bmpWidth + 31) / 32) * 4;

      if (depth == 24)
      { // BGR
        bool allDestsCreated = true;
        img->w = bmpWidth;
        img->h = bmpHeight;

        // Loading to RAM -- allocate GFX 16-bit canvas type
        status = IMAGE_ERR_MALLOC; // Assume won't fit to start
        uint16_t remainingHeight = bmpHeight;
        for (int i = 0; allDestsCreated && remainingHeight > 0 && i < NUM_CANVAS; i++)
        {
          uint16_t canvasHeight = remainingHeight > CANVAS_HEIGHT ? CANVAS_HEIGHT : remainingHeight;
          remainingHeight -= CANVAS_HEIGHT;
          if (!(img->canvas[i] = new GFXcanvas16(bmpWidth, canvasHeight)))
            allDestsCreated = false;
        }

        if (allDestsCreated)
        { // Supported format, alloc OK, etc.
          uint8_t currentCanvasIndex = 0;
          GFXcanvas16 *currentCanvas = img->canvas[currentCanvasIndex];
          uint16_t *currentDest = currentCanvas->getBuffer();
          status = IMAGE_SUCCESS;

          img->format = IMAGE_16; // Is a GFX 16-bit canvas type

          if (depth >= 16)
          {
            for (row = 0; currentCanvasIndex < NUM_CANVAS && row < loadHeight; row++)
            { // For each scanline...; so not process rows larger than image canvas array

              yield(); // Keep ESP8266 happy

              // Seek to start of scan line.  It might seem labor-intensive
              // to be doing this on every line, but this method covers a
              // lot of gritty details like cropping, flip and scanline
              // padding. Also, the seek only takes place if the file
              // position actually needs to change (avoids a lot of cluster
              // math in SD library).
              if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
                bmpPos = offset + (bmpHeight - 1 - (row + loadY)) * rowSize;
              else // Bitmap is stored top-to-bottom
                bmpPos = offset + (row + loadY) * rowSize;
              bmpPos += loadX * 3;
              if (file.position() != bmpPos)
              {                        // Need seek?
                file.seek(bmpPos);     // Seek = SD transaction
                srcidx = sizeof sdbuf; // Force buffer reload
              }
              for (col = 0; col < loadWidth; col++)
              { // For each pixel...
                if (srcidx >= sizeof sdbuf)
                {                                 // Time to load more?
                                                  // Canvas is simpler,
                  file.read(sdbuf, sizeof sdbuf); // just load sdbuf
                  srcidx = 0;                     // Reset bmp buf index
                }
                // Convert each pixel from BMP to 565 format, save in dest
                b = sdbuf[srcidx++];
                g = sdbuf[srcidx++];
                r = sdbuf[srcidx++];
                currentDest[destidx++] =
                    ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

                if (destidx >= currentCanvas->width() * currentCanvas->height())
                {
                  // canvas full, switch to next one
                  destidx = 0;
                  currentCanvasIndex++;
                  if (currentCanvasIndex < NUM_CANVAS)
                  {
                    currentCanvas = img->canvas[currentCanvasIndex];
                    currentDest = currentCanvas->getBuffer();
                  }
                }
              } // end pixel loop
            }   // end scanline loop
          }     // end depth>24 or quantized malloc OK
        }       // end malloc check
      }         // end depth check
    }           // end planes/compression check
  }             // end signature

  file.close();
  return status;
}

/*!
    @brief   Query pixel dimensions of BMP image file on SD card.
    @param   filename
             Name of BMP image file to query.
    @param   width
             Pointer to int32_t; image width in pixels, returned.
    @param   height
             Pointer to int32_t; image height in pixels, returned.
    @return  One of the ImageReturnCode values (IMAGE_SUCCESS on successful
             completion, other values on failure).
*/
ImageReturnCode SPIFFS_ImageReader::bmpDimensions(char *filename,
                                                  int32_t *width,
                                                  int32_t *height)
{

  ImageReturnCode status = IMAGE_ERR_FILE_NOT_FOUND; // Guilty until innocent

  if ((file = SPIFFS.open(filename, FILE_READ)))
  {                            // Open requested file
    status = IMAGE_ERR_FORMAT; // File's there, might not be BMP tho
    if (readLE16() == 0x4D42)
    {                   // BMP signature?
      (void)readLE32(); // Read & ignore file size
      (void)readLE32(); // Read & ignore creator bytes
      (void)readLE32(); // Read & ignore position of image data
      (void)readLE32(); // Read & ignore header size
      if (width)
        *width = readLE32();
      if (height)
      {
        int32_t h = readLE32(); // Don't abs() this, may be a macro
        if (h < 0)
          h = -h; // Do manually instead
        *height = h;
      }
      status = IMAGE_SUCCESS; // YAY.
    }
  }

  file.close();
  return status;
}

// UTILITY FUNCTIONS *******************************************************

/*!
    @brief   Reads a little-endian 16-bit unsigned value from currently-
             open File, converting if necessary to the microcontroller's
             native endianism. (BMP files use little-endian values.)
    @return  Unsigned 16-bit value, native endianism.
*/
uint16_t SPIFFS_ImageReader::readLE16(void)
{
#if !defined(ESP32) && !defined(ESP8266) && \
    (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
  // Read directly into result -- BMP data and variable both little-endian.
  uint16_t result;
  file.read(&result, sizeof result);
  return result;
#else
  // Big-endian or unknown. Byte-by-byte read will perform reversal if needed.
  return file.read() | ((uint16_t)file.read() << 8);
#endif
}

/*!
    @brief   Reads a little-endian 32-bit unsigned value from currently-
             open File, converting if necessary to the microcontroller's
             native endianism. (BMP files use little-endian values.)
    @return  Unsigned 32-bit value, native endianism.
*/
uint32_t SPIFFS_ImageReader::readLE32(void)
{
#if !defined(ESP32) && !defined(ESP8266) && \
    (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
  // Read directly into result -- BMP data and variable both little-endian.
  uint32_t result;
  file.read(&result, sizeof result);
  return result;
#else
  // Big-endian or unknown. Byte-by-byte read will perform reversal if needed.
  return file.read() | ((uint32_t)file.read() << 8) |
         ((uint32_t)file.read() << 16) | ((uint32_t)file.read() << 24);
#endif
}

/*!
    @brief   Print human-readable status message corresponding to an
             ImageReturnCode type.
    @param   stat
             Numeric ImageReturnCode value.
    @param   stream
             Output stream (Serial default if unspecified).
    @return  None (void).
*/
void SPIFFS_ImageReader::printStatus(ImageReturnCode stat, Stream &stream)
{
  if (stat == IMAGE_SUCCESS)
    stream.println(F("Success!"));
  else if (stat == IMAGE_ERR_FILE_NOT_FOUND)
    stream.println(F("File not found."));
  else if (stat == IMAGE_ERR_FORMAT)
    stream.println(F("Not a supported BMP variant."));
  else if (stat == IMAGE_ERR_MALLOC)
    stream.println(F("Malloc failed (insufficient RAM)."));
}
