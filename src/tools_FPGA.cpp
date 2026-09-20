#include "tools_FPGA.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

using namespace std;


/* class exception */

tools::exception::exception(const char *src, const char *fmt, ...) throw()
{
   char buf[4096];
   va_list arg;
   va_start(arg, fmt);
   vsprintf(buf, fmt, arg);
   mess = std::string(src) + " : " + buf;
}

tools::exception::~exception() throw()
{
}

const char * tools::exception::what() const throw()
{
   return mess.c_str();
}


/* functions */

bool tools::isExist(const string& file)
{
   struct stat b;
   return stat(file.c_str(), &b) == 0;
}

template<typename T>
string tools::ttos(T val, int size)
{
   ostringstream oss;
   if (size > 0) oss << setw(size) << setfill('0') << val;
   else          oss << val;

   return oss.str();
}

string tools::shtos (short  val, int size) { return ttos<short>(val, size); }
string tools::ushtos(ushort val, int size) { return ttos<ushort>(val, size); }
string tools::itos  (int    val, int size) { return ttos<int>(val, size); }
string tools::uitos (uint   val, int size) { return ttos<uint>(val, size); }
string tools::ltos  (long   val, int size) { return ttos<long>(val, size); }
string tools::ultos (ulong  val, int size) { return ttos<ulong>(val, size); }

string tools::ftos(float val, int prec)
{
   ostringstream oss;
   if (prec >= 0) {
      oss.precision(prec);
      oss.setf(ios::fixed);
   }
   oss << val;
   return oss.str();
}

uint32_t tools::swapBit(uint32_t x, short b)
{
   uint32_t rx = 0;
   while (b--) {
      rx <<= 1;
      rx |= (x & 1);
      x >>= 1;
   }
   return rx;
}

bool tools::isMatch(const char *str1, const char *str2)
{
   const char *p, *q;
   for (p=str1, q=str2; tolower((uchar)*p) == tolower((uchar)*q); ++p, ++q) {
      if ('\0' == *p) return true;
   }
   return false;
}

bool tools::isMatchExtension(const std::string& file, const char *ext)
{
   int i = file.find_last_of(".");
   std::string extName = file.substr(i, file.size()-i);
   return isMatch(extName.c_str(), ext);
}

int tools::isMatchW(const char *wildcard, const char *target)
{
   const char *pw = wildcard, *pt = target;
   while(1){
      if(*pt == 0){
         while(*pw == '*') pw++;
         return *pw == 0;
      }
      else if(*pw == 0) return 0;
      else if(*pw == '*') {
         return *(pw+1) == 0 || isMatchW(pw, pt + 1)
                             || isMatchW(pw + 1, pt);
      }
      else if(*pw == '?' || (*pw == *pt)){
         pw++;
         pt++;
         continue;
      }
      else return 0;
   }
}

int tools::substr(char *t, const char *s, int pos, int len)
{
   if (pos < 0 || len < 0 || len > strlen(s)) return -1;
   for (s+=pos; *s!='\0' && len>0; len--) *t++ = *s++;
   *t = '\0';
   return 0;
}

#if (_MSC_VER >=1800) || (defined __GNUC__) // vc12 or gcc
void tools::search(const string &pathName, bool clear)
{
   if (clear) {
      fileNames.clear();
      dirNames.clear();
   }
#ifdef __GNUC__
   DIR *dp = opendir(pathName.c_str());
   if (dp) {
      dirent *dent;
      do {
         dent = readdir(dp);
         if (dent) fileNames.push_back(string(dent->d_name));
      } while (dent);
      closedir(dp);
   }
#else
   tr2::sys::path p(pathName);
   for_each(tr2::sys::directory_iterator(p), tr2::sys::directory_iterator(),
//   for_each(tr2::sys::recursive_directory_iterator(p), tr2::sys::recursive_directory_iterator(),
      [](const tr2::sys::path& p) {
         if      (tr2::sys::is_regular_file(p)) fileNames.push_back(p.string());
         else if (tr2::sys::is_directory(p))    dirNames.push_back(p.string());
      }
   );
#endif
}

vector<string> tools::getFileNames(const string &pathName, bool clear)
{
   search(pathName, clear);
   return fileNames;
}

vector<string> tools::getDirNames(const string &pathName, bool clear)
{
   search(pathName, clear);
#ifdef __GNUC__
   return fileNames;
#else
   return dirNames;
#endif
}

vector<string> tools::getMatchFileNames(const std::string &pathName, const char *wildcard)
{
   search(pathName, true);
   vector<string> matchFileNames;
   for (int i=0; i<fileNames.size(); i++) {
      if (isMatchW(wildcard, fileNames[i].c_str()) == 1) matchFileNames.push_back(fileNames[i]);
   }
   return matchFileNames;
}
#endif

vector<string> tools::getSplitNames(const string &str, char delim)
{
   vector<string> res;
   string tmp;
   istringstream iss(str);
   while (getline(iss, tmp, delim)) res.push_back(tmp);
   return res;
}

string tools::getFirstName(const string &str, char delim)
{
   vector<string> res = getSplitNames(str, delim);
   int n = res.size();
   return n > 0 ? res[0] : "null";
}

string tools::getLastName(const string &str, char delim)
{
   vector<string> res = getSplitNames(str, delim);
   int n = res.size();
   return n > 0 ? res[n-1] : "null";
}
