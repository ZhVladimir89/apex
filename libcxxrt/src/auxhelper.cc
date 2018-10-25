/* 
 * Copyright 2010-2011 PathScale, Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * aux.cc - Compiler helper functions. 
 *
 * The functions declared in this file are intended to be called only by code
 * that is automatically generated by C++ compilers for some common cases.  
 */

#include <stdlib.h>
#include "stdexcept.h"

#include <debug.h>

/**
 * Called to generate a bad cast exception.  This function is intended to allow
 * compilers to insert code generating this exception without needing to
 * duplicate the code for throwing the exception in every call site.
 */
extern "C" void __cxa_bad_cast()
{
    panic("bad_cast");
}

/**
 * Called to generate a bad typeid exception.  This function is intended to
 * allow compilers to insert code generating this exception without needing to
 * duplicate the code for throwing the exception in every call site.
 */
extern "C" void __cxa_bad_typeid()
{
    panic("bad_typeid");
}

/**
 * Compilers may (but are not required to) set any pure-virtual function's
 * vtable entry to this function.  This makes debugging slightly easier, as
 * users can add a breakpoint on this function to tell if they've accidentally
 * called a pure-virtual function.
 */
extern "C" void __cxa_pure_virtual()
{
    panic("pure_virtual");
}

/**
 * Compilers may (but are not required to) set any deleted-virtual function's
 * vtable entry to this function.  This makes debugging slightly easier, as
 * users can add a breakpoint on this function to tell if they've accidentally
 * called a deleted-virtual function.
 */
extern "C" void __cxa_deleted_virtual()
{
    panic("deleted_virtual");
}

extern "C" void __cxa_throw_bad_array_new_length()
{
    panic("bad_array_new_length");
}
