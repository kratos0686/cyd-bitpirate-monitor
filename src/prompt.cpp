#include "prompt.h"
#include <ctype.h>
#include <string.h>

bool parsePromptMode(const char* line, char* out, int cap) {
  int n = 0;
  while (line[n] && (isalnum((unsigned char)line[n]) || line[n] == '_')) n++;
  if (n == 0 || n > 12 || n >= cap) return false;
  if (line[n] != '>') return false;
  if (line[n + 1] != '\0' && line[n + 1] != ' ') return false;
  memcpy(out, line, n);
  out[n] = 0;
  return true;
}
