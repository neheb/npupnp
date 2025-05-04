/* Copyright (C) 2016-2025 J.F.Dockes
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "netif.h"

#include <string>
#include <iostream>

static int       op_flags;
#define OPT_i    0x1
#define OPT_f    0x2

static char *thisprog;
static char usage [] =
    " Default: dump interface configuration\n"
    "-i <testaddr>  : find interface for address.\n"
    "-f             : filter for non-loopback+ipv4.\n"
    ;

static void Usage(void)
{
    std::cerr << thisprog << ": usage:\n" << usage;
    exit(1);
}

int main(int argc, char *argv[])
{
    std::string addr;
    thisprog = argv[0];
    argc--; argv++;
    while (argc > 0 && **argv == '-') {
        (*argv)++;
        if (!(**argv))
            Usage();
        while (**argv)
            switch (*(*argv)++) {
            case 'i': op_flags |= OPT_i; if (argc < 2)  Usage();
                addr = *(++argv); argc--; 
                goto b1;
            case 'f': op_flags |= OPT_f;break;
            default: Usage();   break;
            }
    b1: argc--; argv++;
    }

    NetIF::Interfaces *ifs = NetIF::Interfaces::theInterfaces();
    if (op_flags == 0) {
        ifs->print(std::cout);
    } else if (op_flags & OPT_i) {
        NetIF::IPAddr ipaddr(addr);
        if (!ipaddr.ok()) {
            std::cerr << "Address parse failed for [" << addr << "]\n";
            return 1;
        }

        NetIF::Interfaces::Filter filt;
        std::vector<NetIF::Interface> vifs = ifs->select(filt);
        NetIF::IPAddr haddr;
        const NetIF::Interface *the_if = NetIF::Interfaces::interfaceForAddress(ipaddr, vifs, haddr);

        if (nullptr == the_if) {
            std::cerr << "No interface found for " << ipaddr.straddr() << "\n";
            return 1;
        }
        std::cout << "Interface for " << ipaddr.straddr() << " : \n\n";
        the_if->print(std::cout);
        std::cout << " \nhost address: " << haddr.straddr() << "\n";
    } else if (op_flags & OPT_f) {
        NetIF::Interfaces::Filter filt{
            .needs={NetIF::Interface::Flags::HASIPV4},
            .rejects={NetIF::Interface::Flags::LOOPBACK}};
        std::vector<NetIF::Interface> myifs = ifs->select(filt);
        std::cout << "\nSelected:\n";
        for (const auto& entry : myifs)
            entry.print(std::cout);
    } else {
        Usage();
    }
    return 0;
}
