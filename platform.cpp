#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#else
//#error ("TODO: Link to OS socket library.")
#endif