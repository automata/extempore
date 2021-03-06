/*
 * Copyright (c) 2011, Andrew Sorensen
 *
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 *
 * Neither the name of the authors nor other contributors may be used to endorse
 * or promote products derived from this software without specific prior written 
 * permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLEXTD. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <iostream>

#include "pthread.h"

#include "EXTMonitor.h"


#define _EXTMONITOR_DEBUG_


namespace extemp
{
    EXTMonitor::EXTMonitor(std::string _name) :
	mutex(_name),
	name(_name)
    {
        initialised = false;
        init();
    }
    
    
    EXTMonitor::~EXTMonitor()
    {
        destroy();
    }
    

    void EXTMonitor::init()
    {
        if (! initialised)
        {
            mutex.init();
            condition.init();
            initialised = true;
        }
    }


    void EXTMonitor::destroy()
    {
        if (initialised)
        {
            initialised = false;
            mutex.destroy();
            condition.destroy();
        }
    }

    
    int EXTMonitor::lock()
    {
        int result = mutex.lock();        
        return result;
    }


    int EXTMonitor::unlock()
    {
        int result = mutex.unlock();
        return result;
    }


    int EXTMonitor::wait()
    {
        int result = condition.wait(&mutex);
        return result;
    }


    int EXTMonitor::signal()
    {
        int result = condition.signal();
        return result;
    }

    bool EXTMonitor::isOwnedByCurrentThread()
    {
	mutex.isOwnedByCurrentThread();	
    }
}
