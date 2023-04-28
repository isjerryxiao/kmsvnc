#pragma once

#define VA_MUST(x) do{VAStatus s; if ((s = (x)) != VA_STATUS_SUCCESS) KMSVNC_FATAL("va operation error %#x %s on line %d\n", s, vaErrorStr(s), __LINE__); } while (0)
#define VA_MAY(x) do{VAStatus s; if ((s = (x)) != VA_STATUS_SUCCESS) fprintf(stderr, "va operation error %#x %s on line %d\n", s, vaErrorStr(s), __LINE__); } while (0)

void va_cleanup();
int va_init();
int va_hwframe_to_vaapi(char *out);
