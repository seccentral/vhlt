#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef SYSTEM_WIN32
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>
#endif

#ifdef SYSTEM_POSIX
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#endif

#include "cmdlib.h"
#include "messages.h"
#include "log.h"
#include "mathtypes.h"
#include "mathlib.h"
#include "blockmem.h"

/*
 * ==============
 * getfiletime
 * ==============
 */

time_t getfiletime(const char* const filename)
{
    time_t filetime = 0;
    struct stat filestat;

    if (stat(filename, &filestat) == 0)
        filetime = qmax(filestat.st_mtime, filestat.st_ctime);

    return filetime;
}

/*
 * ==============
 * getfilesize
 * ==============
 */
long getfilesize(const char* const filename)
{
    long size = 0;
    struct stat filestat;

    if (stat(filename, &filestat) == 0)
        size = filestat.st_size;

    return size;
}

/*
 * ==============
 * getfiledata
 * ==============
 */
long getfiledata(const char* const filename, char* buffer, const int buffersize)
{
    long size = 0;
    int handle;
    time_t start, end;

    time(&start);

    handle = _open(filename, O_RDONLY);
    if (handle != -1)
    {
        int bytesread;

        Log("%-20s Restoring [%-13s - ", "BuildVisMatrix:", filename);
        while (size < buffersize && (bytesread = _read(handle, buffer, qmin(32 * 1024, buffersize - size))) > 0)
        {
            size += bytesread;
            buffer += bytesread;
        }
        _close(handle);
        time(&end);
        Log("%10.3fMB] (%d)\n", size / (1024.0 * 1024.0), (int)(end - start));
    }

    if (buffersize != size)
    {
        Warning("Invalid file [%s] found.  File will be rebuilt!\n", filename);
        _unlink(filename);
    }

    return size;
}

/*
 * ================
 * filelength
 * ================
 */
long q_filelength(FILE* f)
{
    long pos;
    long end;

    pos = ftell(f);
    if (pos == -1L)
    {
        return -1L;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        return -1L;
    }
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

/*
 * ================
 * exists
 * ================
 */
bool q_exists(const char* const filename)
{
    FILE* f;

    f = fopen(filename, "rb");

    if (!f)
    {
        IfDebug(Developer(DEVELOPER_LEVEL_SPAM, "Checking for existance of file %s (failed)\n", filename));
        return false;
    }
    else
    {
        fclose(f);
        IfDebug(Developer(DEVELOPER_LEVEL_SPAM, "Checking for existance of file %s (success)\n", filename));
        return true;
    }
}


FILE* SafeOpenWrite(const char* const filename)
{
    FILE* f;

    f = fopen(filename, "wb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

FILE* SafeOpenRead(const char* const filename)
{
    FILE* f;

    f = fopen(filename, "rb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

void SafeRead(FILE* f, void* buffer, int count)
{
    if (fread(buffer, 1, count, f) != (size_t)count)
        Error("File read failure");
}

void SafeWrite(FILE* f, const void* const buffer, int count)
{
    if (fwrite(buffer, 1, count, f) != (size_t)count)
        Error("File write failure"); //Error("File read failure"); //--vluzacn
}

/*
 * ==============
 * LoadFile
 * ==============
 */
int LoadFile(const char* const filename, char** bufferptr)
{
    FILE* f;
    long length;
    char* buffer;

    f = SafeOpenRead(filename);
    length = q_filelength(f);
    if (length < 0)
    {
        fclose(f);
        Error("LoadFile: invalid file length");
    }
    if (length > INT_MAX)
    {
        fclose(f);
        Error("LoadFile: file too large");
    }
    buffer = (char*)Alloc((unsigned long)length + 1);
    SafeRead(f, buffer, (int)length);
    fclose(f);

    *bufferptr = buffer;
    return (int)length;
}

/*
 * ==============
 * SaveFile
 * ==============
 */
void SaveFile(const char* const filename, const void* const buffer, int count)
{
    FILE* f;

    f = SafeOpenWrite(filename);
    SafeWrite(f, buffer, count);
    fclose(f);
}
