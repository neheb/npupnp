/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#include "config.h"

#include "upnp.h"
#include "upnpdebug.h"
#include "smallut.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

/* Mutex to synchronize all the log file operations in the debug mode */
static std::mutex GlobalDebugMutex;

/* Global log level */
static Upnp_LogLevel g_log_level = UPNP_DEFAULT_LOG_LEVEL;

/* Output file pointer */
static FILE *o_fp;
static int o_is_stderr;

/* Set if the user called setlogfilename() or setloglevel() */
static int o_setlogwascalled;
/* Name of the output file. We keep a copy */
static std::string o_fileName;

enum SubSysDebugFlag {SSDF_NONE=0, SSDF_SSDP=0x1, SSDF_SOAP=0x2, SSDF_GENA=0x4, SSDF_TPOOL=0x8,
                      SSDF_MSERV=0x10, SSDF_DOM=0x20, SSDF_HTTP=0x40, SSDF_ALL=0xFFFF};

static unsigned int subsysdebug = SSDF_ALL;
static bool subsysdone;

static void setModuleFlags()
{
    const char *cp = getenv("NPUPNP_DEBUGMODULES");
    if (cp) {
        std::string scp(cp);
        bool neg = false;
        if (!scp.empty() && scp[0] == '-') {
            neg = true;
            subsysdebug = SSDF_ALL;
        } else {
            subsysdebug = SSDF_NONE;
        }
        if (neg)
            scp = scp.substr(1);
        std::vector<std::string> modules;
        stringToStrings(scp, modules, ",;");
        for (const auto& mod : modules) {
            if (mod == "SSDP") {
                if (neg) {
                    subsysdebug &= ~SSDF_SSDP;
                } else {
                    subsysdebug |= SSDF_SSDP;
                }
            } else if (mod == "SOAP") {
                if (neg) {
                    subsysdebug &= ~SSDF_SOAP;
                } else {
                    subsysdebug |= SSDF_SOAP;
                }
            } else if (mod == "GENA") {
                if (neg) {
                    subsysdebug &= ~SSDF_GENA;
                } else {
                    subsysdebug |= SSDF_GENA;
                }
            } else if (mod == "TPOOL") {
                if (neg) {
                    subsysdebug &= ~SSDF_TPOOL;
                } else {
                    subsysdebug |= SSDF_TPOOL;
                }
            } else if (mod == "MSERV") {
                if (neg) {
                    subsysdebug &= ~SSDF_MSERV;
                } else {
                    subsysdebug |= SSDF_MSERV;
                }
            } else if (mod == "DOM") {
                if (neg) {
                    subsysdebug &= ~SSDF_DOM;
                } else {
                    subsysdebug |= SSDF_DOM;
                }
            } else if (mod == "HTTP") {
                if (neg) {
                    subsysdebug &= ~SSDF_HTTP;
                } else {
                    subsysdebug |= SSDF_HTTP;
                }
            }
        }
    }
}

/* This can be called multiple times, for example to rotate the log file.*/
int UpnpInitLog(void)
{
    if (o_setlogwascalled == 0) {
        const char* envlevel = getenv("NPUPNP_LOGLEVEL");
        const char* envfn = getenv("NPUPNP_LOGFILENAME");
        /* Maybe a call from UpnpInit(). If the user did not ask for
           logging do nothing, else init from environment or filename
           and level from the api calls */
        if (nullptr == envlevel && nullptr == envfn) {
            return UPNP_E_SUCCESS;
        }
        if (envlevel) {
            g_log_level = static_cast<Upnp_LogLevel>(atoi(envlevel));
        }
        if (envfn) {
            o_fileName = envfn;
        }
    }
    if (o_fp && !o_is_stderr) {
        fclose(o_fp);
        o_fp = nullptr;
        o_is_stderr = 0;
    }
    if (!o_fileName.empty()) {
        if ((o_fp = fopen(o_fileName.c_str(), "a")) == nullptr) {
            std::cerr << "UpnpDebug: failed to open [" << o_fileName << "] : " <<
                strerror(errno) << "\n";
        }
        o_is_stderr = 0;
    }
    if (o_fp == nullptr) {
        o_fp = stderr;
        o_is_stderr = 1;
    }
    return UPNP_E_SUCCESS;
}

void UpnpSetLogLevel(Upnp_LogLevel log_level)
{
    g_log_level = log_level;
    o_setlogwascalled = 1;
}

void UpnpCloseLog(void)
{
    std::scoped_lock lck(GlobalDebugMutex);

    if (o_fp != nullptr && o_is_stderr == 0) {
        fclose(o_fp);
    }
    o_fp = nullptr;
    o_is_stderr = 0;
}

void UpnpSetLogFileNames(const char *newFileName, const char *ignored)
{
    (void)ignored;

    o_fileName.clear();
    if (newFileName && *newFileName) {
        o_fileName = newFileName;
    }
    o_setlogwascalled = 1;
}

static int DebugAtThisLevel(Upnp_LogLevel DLevel, Dbg_Module Module)
{
    if (!subsysdone) {
        setModuleFlags();
        subsysdone = true;
    }
    return (DLevel <= g_log_level) &&
        ( (Module == SSDP && (subsysdebug & SSDF_SSDP)) ||
          (Module == SOAP && (subsysdebug & SSDF_SOAP)) ||
          (Module == GENA && (subsysdebug & SSDF_GENA)) ||
          (Module == TPOOL && (subsysdebug & SSDF_TPOOL)) ||
          (Module == MSERV && (subsysdebug & SSDF_MSERV)) ||
          (Module == DOM && (subsysdebug & SSDF_DOM)) ||
          (Module == HTTP && (subsysdebug & SSDF_HTTP)));
}

static void UpnpDisplayFileAndLine(
    FILE *fp, const char *DbgFileName,
    int DbgLineNo, Upnp_LogLevel DLevel, Dbg_Module Module)
{
    char timebuf[26];
    time_t now = time(nullptr);
    const struct tm *timeinfo;
    const char *smod;

    auto slev = std::to_string(DLevel);
        
    switch(Module) {
    case SSDP: smod="SSDP";break;
    case SOAP: smod="SOAP";break;
    case GENA: smod="GENA";break;
    case TPOOL: smod="TPOL";break;
    case MSERV: smod="MSER";break;
    case DOM: smod="DOM_";break;
    case API: smod="API_";break;
    case HTTP: smod="HTTP";break;
    default: smod="UNKN";break;
    }

    timeinfo = localtime(&now);
    strftime(timebuf, 26, "%Y-%m-%d %H:%M:%S", timeinfo);
    std::ostringstream ss;
    ss << "0x" << std::hex << std::this_thread::get_id();
    fprintf(fp, "%s UPNP-%s-%s: Thread:%s [%s:%d]: ", timebuf, smod, slev.c_str(),
            ss.str().c_str(), DbgFileName, DbgLineNo);
    fflush(fp);
}

void UpnpPrintf(
    Upnp_LogLevel DLevel, Dbg_Module Module,
    const char *DbgFileName, int DbgLineNo, const char *FmtStr, ...)
{
    /*fprintf(stderr, "UpnpPrintf: fp %p level %d glev %d mod %d DEBUG_ALL %d\n",
      fp, DLevel, g_log_level, Module, DEBUG_ALL);*/
    va_list ArgList;

    if (!DebugAtThisLevel(DLevel, Module))
        return;

    std::scoped_lock lck(GlobalDebugMutex);
    if (o_fp == nullptr) {
        return;
    }

    va_start(ArgList, FmtStr);
    if (DbgFileName) {
        UpnpDisplayFileAndLine(o_fp, DbgFileName, DbgLineNo, DLevel, Module);
        vfprintf(o_fp, FmtStr, ArgList);
        fflush(o_fp);
    }
    va_end(ArgList);
}

/* No locking here, the app should be careful about not calling
   closelog from a separate thread... */
FILE *UpnpGetDebugFile(Upnp_LogLevel DLevel, Dbg_Module Module)
{
    if (!DebugAtThisLevel(DLevel, Module)) {
        return nullptr;
    }
    return o_fp;
}
