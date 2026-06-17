// stb_image_impl.cpp — single translation unit that compiles the stb_image
// PNG decoder used by the launcher to load box art / controller / console art.
// PNG-only keeps the binary small; the launcher's LoadTexture override decodes
// straight here. (Pattern from the PSX launcher.)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
