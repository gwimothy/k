// stb_image implementation, compiled once.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO // we load through the virtual filesystem
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#include <stb_image.h>
