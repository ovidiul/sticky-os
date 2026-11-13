// Photos from the microSD card, turned into the panel's four greys.
#pragma once
#include <Arduino.h>

bool photosMount();   // mounts the card and lists the JPEGs; false with no card or none found
bool photosCardIn();
int photosCount();

// Decodes photo `index` into `out`: w x h pixels, 2 bits each, four to a byte
// with the leftmost in the top bits; 0 is black, 3 white. Landscape photos
// fill the area (cropped around the centre); portrait ones fit inside it.
// `name` gets the file name. False if the file can't be shown; `why` says why.
bool photoRender(int index, uint8_t *out, int w, int h, char *name, size_t nameSize,
                 const char **why);
