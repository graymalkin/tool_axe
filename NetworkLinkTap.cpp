// Copyright (c) 2012, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#if defined(__linux__)
#include "NetworkLinkTapLinux.cpp"
#elif defined(__APPLE__)
#include "NetworkLinkTapOSX.cpp"
#else
// TODO Windows
#include "NetworkLinkTapDefault.cpp"
#endif
