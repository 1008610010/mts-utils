/*
 * Filter a transport stream by a list of pids.
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the MPEG TS, PS and ES tools.
 *
 * The Initial Developer of the Original Code is Amino Communications Ltd.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Amino Communications Ltd, Swavesey, Cambridge UK
 *
 * ***** END LICENSE BLOCK *****
 */

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "accessunit.h"
#include "bitdata.h"
#include "compat.h"
#include "es.h"
#include "h222.h"
#include "h262.h"
#include "misc.h"
#include "nalunit.h"
#include "pes.h"
#include "pidint.h"
#include "printing.h"
#include "ps.h"
#include "reverse.h"
#include "ts.h"
#include "tswrite.h"
#include "version.h"

/** List of PIDs to filter in */
int* pidList = nullptr;
unsigned int pidListAlloc = 0, pidListUsed = 0;

static void print_usage(void);

void ensurePidList(int nr)
{
    if (pidListAlloc > nr) {
        return;
    }
    pidListAlloc = nr;
    pidList = (int*)realloc(pidList, pidListAlloc * sizeof(int));
}

int main(int argn, char* args[])
{
    int ii = 1;
    //    int verbose = false;  // Currently unused - squash warning
    int invert = 0;
    unsigned int max_pkts = (unsigned int)-1;
    const char *input_file = nullptr, *output_file = nullptr;

    if (argn < 2) {
        print_usage();
        return 0;
    }

    ensurePidList(1024);

    while (ii < argn) {
        if (args[ii][0] == '-') {
            if (!strcmp("--help", args[ii]) || !strcmp("-h", args[ii])
                || !strcmp("-help", args[ii])) {
                print_usage();
                return 0;
            } else if (!strcmp("-verbose", args[ii]) || !strcmp("-v", args[ii])) {
                //                verbose = true;
            } else if (!strcmp("-m", args[ii]) || !strcmp("-max", args[ii])) {
                if (argn <= ii) {
                    fprint_err("### tsfilter: -max requires an argument\n");
                    return 1;
                }
                max_pkts = atoi(args[ii + 1]);
                ++ii;
            } else if (!strcmp("-!", args[ii]) || !strcmp("-invert", args[ii])) {
                invert = 1;
            } else if (!strcmp("-i", args[ii]) || !strcmp("-input", args[ii])) {
                if (argn <= ii) {
                    fprint_err("### tsfilter: -input requires an argument\n");
                    return 1;
                }
                input_file = args[ii + 1];
                ++ii;
            } else if (!strcmp("-o", args[ii]) || !strcmp("-output", args[ii])) {
                if (argn <= ii) {
                    fprint_err("### tsfilter: -output requires an argument\n");
                    return 1;
                }
                output_file = args[ii + 1];
                ++ii;
            } else {
                fprint_err("### tsfilter: "
                           "Unrecognised command line switch '%s'\n",
                    args[ii]);
                return 1;
            }
        } else {
            char* p = nullptr;
            // It's a pid.
            if (pidListUsed >= pidListAlloc) {
                ensurePidList(pidListAlloc + 1024);
            }

            pidList[pidListUsed] = strtoul(args[ii], &p, 0);
            if (!(p && *p == '\0')) {
                fprint_err("### tsfilter: '%s' wasn't a valid number. \n", args[ii]);
                return 1;
            }
            ++pidListUsed;
        }
        ++ii;
    }

    if (!pidListUsed) {
        fprint_err("### tsfilter: No pids to filter. \n");
        return 1;
    }

    // Now ..
    {
        int err;
        TS_reader_p tsreader;
        TS_writer_p tswriter;
        byte* pkt = nullptr;

        unsigned int pid, pkt_num;
        int pusi, adapt_len, payload_len;
        byte *adapt, *payload;

        pkt_num = 0;
        err = open_file_for_TS_read((char*)input_file, &tsreader);
        if (err) {
            fprint_err("## tsfilter: Unable to open stdin for reading TS.\n");
            return 1;
        }
        if (output_file) {
            err = tswrite_open(TS_W_FILE, (char*)output_file, nullptr, 0, 1, &tswriter);
        } else {
            err = tswrite_open(TS_W_STDOUT, nullptr, nullptr, 0, 1, &tswriter);
        }
        if (err) {
            fprint_err("## tsfilter: Unable to open stdout for writing TS. \n");
            return 1;
        }

        while (1) {
            err = read_next_TS_packet(tsreader, &pkt);
            if (err == EOF) {
                /* We're done */
                break;
            }

            err = split_TS_packet(pkt, &pid, &pusi, &adapt, &adapt_len, &payload, &payload_len);
            if (err) {
                fprint_err("### Error splitting TS packet - continuing. \n");
            } else {
                int i;
                int found = 0;

                for (i = 0; i < pidListUsed; ++i) {
                    if (pid == pidList[i]) {
                        ++found; // Yes!
                        break;
                    }
                }

                if (max_pkts != (unsigned int)-1 && pkt_num > max_pkts) {
                    // We're done processing. If invert is on,
                    // copy the rest of the output, otherwise quit.
                    if (!invert) {
                        break;
                    } else {
                        found = 0;
                    }
                }

                // Invert the result, whatever it was.
                if (invert) {
                    found = !found;
                }

                if (found) {
                    // Write it out.
                    err = tswrite_write(tswriter, pkt, pid, 0, 0);

                    if (err) {
                        fprint_err("### Error writing output - %d \n", err);
                        return 2;
                    }
                }
                ++pkt_num;
            }
        }

        // It's the end!
        tswrite_close(tswriter, 1);
        close_TS_reader(&tsreader);
        return 0;
    }
}

static void print_usage(void)
{
    print_msg("Usage: tsfilter [switches] <pid> <pid> <pid> ... \n"
              "\n");
    REPORT_VERSION("tsfilter");
    print_msg("\n"
              " Filter the given pids out of stdin and write the result on stdout.\n"
              "\n"
              "Switches:\n"
              "  -i <infile>      Take input from this file and not stdin.\n"
              "  -o <outfile>     Send output to this file and not stdout.\n"
              "  -verbose, -v     Be verbose.\n"
              "  -max <n>, -m <n> All packets after the nth are regarded as\n"
              "                    not matching any pids.\n"
              "  -!, -invert      Invert whatever your decision was before \n"
              "                    applying it - the output contains only  \n"
              "                    pids not in the list up to max packets  \n"
              "                    and all packets in the input from then  \n"
              "                    on.\n");
}
