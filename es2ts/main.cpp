/*
 * Convert an Elementary Stream to Transport Stream.
 *
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

/*
 * Write (copy) the current ES data unit to the output stream, wrapped up in a
 * PES within TS.
 *
 * Returns 0 if all went well, 1 if something went wrong.
 */
static int write_ES_unit_as_TS(TS_writer_p output, ES_unit_p unit, uint32_t video_pid)
{
    int err = write_ES_as_TS_PES_packet(
        output, unit->data, unit->data_len, video_pid, DEFAULT_VIDEO_STREAM_ID);
    if (err) {
        print_err("### Error writing ES data unit\n");
        return err;
    } else
        return 0;
}

static int transfer_data(ES_p es, TS_writer_p output, uint32_t pmt_pid, uint32_t video_pid,
    byte stream_type, int max2, int verbose, int quiet)
{
    int err = 0;
    int count = 0;

    // Write out a PAT and PMT first, or our stream won't make sense
    if (!quiet)
        fprint_msg("Using transport stream id 1, PMT PID %#x, program 1 ="
                   " PID %#x, stream type %#x\n",
            pmt_pid, video_pid, stream_type);
    err = write_TS_program_data(output, 1, 1, pmt_pid, video_pid, stream_type);
    if (err) {
        print_err("### Error writing out TS program data\n");
        return 1;
    }

    for (;;) {
        ES_unit_p unit;

        err = find_and_build_next_ES_unit(es, &unit);
        if (err == EOF)
            break;
        else if (err) {
            print_err("### Error copying ES data units\n");
            return err;
        }
        count++;

        if (verbose)
            report_ES_unit(false, unit);

        err = write_ES_unit_as_TS(output, unit, video_pid);
        if (err) {
            free_ES_unit(&unit);
            print_err("### Error copying ES data units\n");
            return err;
        }

        free_ES_unit(&unit);

        if (max2 > 0 && count >= max2)
            break;
    }
    if (!quiet)
        fprint_msg("Transferred %d ES data unit%s\n", count, (count == 1 ? "" : "s"));
    return 0;
}

static void print_usage()
{
    print_msg("Usage: es2ts [switches] [<infile>] [<outfile>]\n"
              "\n");
    REPORT_VERSION("es2ts");
    print_msg("\n"
              "  Convert an elementary video stream to H.222 transport stream.\n"
              "  Supports input streams conforming to MPEG-2 (H.262), MPEG-4/AVC\n"
              "  (H.264) and AVS. Also supports MPEG-1 input streams, insofar as MPEG-2\n"
              "  is backwards compatible with MPEG-1.\n"
              "\n"
              "  Note that this program works by reading and packaging the elementary\n"
              "  stream packages directly - it does not parse them as H.262 or H.264\n"
              "  data.\n"
              "\n"
              "Files:\n"
              "  <infile>          is a file containing the Elementary Stream data\n"
              "                    (but see -stdin below)\n"
              "  <outfile>         is an H.222 Transport Stream file\n"
              "                    (but see -stdout and -host below)\n"
              "\n"
              "Switches:\n"
              "  -pid <pid>        <pid> is the video PID to use for the data.\n"
              "                    Use '-pid 0x<pid>' to specify a hex value.\n"
              "                    Defaults to 0x68.\n"
              "  -pmt <pid>       <pid> is the PMT PID to use.\n"
              "                     Use '-pmt 0x<pid>' to specify a hex value.\n"
              "                     Defaults to 0x66\n"
              "  -verbose, -v      Output summary information about each ES packet\n"
              "                    as it is read\n"
              "  -quiet, -q        Only output error messages\n"
              "  -err stdout       Write error messages to standard output (the default)\n"
              "  -err stderr       Write error messages to standard error (Unix traditional)\n"
              "  -stdin            Take input from <stdin>, instead of a named file\n"
              "  -stdout           Write output to <stdout>, instead of a named file\n"
              "                    Forces -quiet and -err stderr.\n"
              "  -host <host>, -host <host>:<port>\n"
              "                    Writes output (over TCP/IP) to the named <host>,\n"
              "                    instead of to a named file. If <port> is not\n"
              "                    specified, it defaults to 88.\n"
              "  -max <n>, -m <n>  Maximum number of ES data units to read\n"
              "\n"
              "Stream type:\n"
              "  When the TS data is being output, it is flagged to indicate whether\n"
              "  it conforms to H.262, H.264 or AVS. It is important to get this right,\n"
              "  as it will affect interpretation of the TS data.\n"
              "\n"
              "  If input is from a file, then the program will look at the start of\n"
              "  the file to determine if the stream is H.264, H.262 or AVS. This\n"
              "  process may occasionally come to the wrong conclusion, in which case\n"
              "  the user can override the choice using the following switches.\n"
              "\n"
              "  If input is from standard input (via -stdin), then it is not possible\n"
              "  for the program to make its own decision on the input stream type.\n"
              "  Instead, it defaults to H.262, and relies on the user indicating if\n"
              "  this is wrong.\n"
              "\n"
              "  -h264, -avc       Force the program to treat the input as MPEG-4/AVC.\n"
              "  -h262             Force the program to treat the input as MPEG-2.\n"
              "  -avs              Force the program to treat the input as AVS.\n");
}

int main(int argc, char** argv)
{
    int use_stdin = false;
    int use_stdout = false;
    int use_tcpip = false;
    int port = 88; // Useful default port number
    char* input_name = nullptr;
    char* output_name = nullptr;
    int had_input_name = false;
    int had_output_name = false;
    TS_writer_p output = nullptr;
    ES_p es;
    int verbose = false;
    int quiet = false;
    int max2 = 0;
    uint32_t video_pid = 0x68;
    uint32_t pmt_pid = 0x66;
    int err = 0;
    int err2;
    int ii = 1;

    int video_type = VIDEO_H262; // hopefully a sensible default
    int force_stream_type = false;
    byte stream_type = 0; // silly value to keep compiler quiet

    if (argc < 2) {
        print_usage();
        return 0;
    }

    while (ii < argc) {
        if (argv[ii][0] == '-') {
            if (!strcmp("--help", argv[ii]) || !strcmp("-help", argv[ii])
                || !strcmp("-h", argv[ii])) {
                print_usage();
                return 0;
            } else if (!strcmp("-avc", argv[ii]) || !strcmp("-h264", argv[ii])) {
                force_stream_type = true;
                video_type = VIDEO_H264;
            } else if (!strcmp("-h262", argv[ii])) {
                force_stream_type = true;
                video_type = VIDEO_H262;
            } else if (!strcmp("-avs", argv[ii])) {
                force_stream_type = true;
                video_type = VIDEO_AVS;
            } else if (!strcmp("-stdin", argv[ii])) {
                had_input_name = true; // more or less
                use_stdin = true;
            } else if (!strcmp("-stdout", argv[ii])) {
                had_output_name = true; // more or less
                use_stdout = true;
                redirect_output_stderr();
            } else if (!strcmp("-err", argv[ii])) {
                CHECKARG("es2ts", ii);
                if (!strcmp(argv[ii + 1], "stderr"))
                    redirect_output_stderr();
                else if (!strcmp(argv[ii + 1], "stdout"))
                    redirect_output_stdout();
                else {
                    fprint_err("### es2ts: "
                               "Unrecognised option '%s' to -err (not 'stdout' or"
                               " 'stderr')\n",
                        argv[ii + 1]);
                    return 1;
                }
                ii++;
            } else if (!strcmp("-host", argv[ii])) {
                CHECKARG("es2ts", ii);
                err = host_value("es2ts", argv[ii], argv[ii + 1], &output_name, &port);
                if (err)
                    return 1;
                had_output_name = true; // more or less
                use_tcpip = true;
                ii++;
            } else if (!strcmp("-verbose", argv[ii]) || !strcmp("-v", argv[ii])) {
                verbose = true;
                quiet = false;
            } else if (!strcmp("-quiet", argv[ii]) || !strcmp("-q", argv[ii])) {
                verbose = false;
                quiet = true;
            } else if (!strcmp("-max", argv[ii]) || !strcmp("-m", argv[ii])) {
                CHECKARG("es2ts", ii);
                err = int_value("es2ts", argv[ii], argv[ii + 1], true, 10, &max2);
                if (err)
                    return 1;
                ii++;
            } else if (!strcmp("-pid", argv[ii])) {
                CHECKARG("es2ts", ii);
                err = unsigned_value("es2ts", argv[ii], argv[ii + 1], 0, &video_pid);
                if (err)
                    return 1;
                ii++;
            } else if (!strcmp("-pmt", argv[ii])) {
                CHECKARG("es2ts", ii);
                err = unsigned_value("es2ts", argv[ii], argv[ii + 1], 0, &pmt_pid);
                if (err)
                    return 1;
                ii++;
            } else {
                fprint_err("### es2ts: "
                           "Unrecognised command line switch '%s'\n",
                    argv[ii]);
                return 1;
            }
        } else {
            if (had_input_name && had_output_name) {
                fprint_err("### es2ts: Unexpected '%s'\n", argv[ii]);
                return 1;
            } else if (had_input_name) {
                output_name = argv[ii];
                had_output_name = true;
            } else {
                input_name = argv[ii];
                had_input_name = true;
            }
        }
        ii++;
    }

    if (!had_input_name) {
        print_err("### es2ts: No input file specified\n");
        return 1;
    }
    if (!had_output_name) {
        print_err("### es2ts: No output file specified\n");
        return 1;
    }

    // Try to stop extraneous data ending up in our output stream
    if (use_stdout) {
        verbose = false;
        quiet = true;
    }

    if (use_stdin)
        err = open_elementary_stream(nullptr, &es);
    else
        err = open_elementary_stream(input_name, &es);
    if (err) {
        print_err("### es2ts: "
                  "Problem starting elementary stream - abandoning reading\n");
        return 1;
    }

    if (!quiet)
        fprint_msg("Reading from  %s\n", (use_stdin ? "<stdin>" : input_name));

    // Decide if the input stream is H.262 or H.264
    if (force_stream_type || use_stdin) {
        if (!quiet)
            print_msg("Reading input as ");
    } else {
        // int video_type;
        err = decide_ES_file_video_type(es->input, false, verbose, &video_type);
        if (err) {
            print_err("### es2ts: Error deciding on stream type\n");
            close_elementary_stream(&es);
            return 1;
        }
        if (!quiet)
            print_msg("Input appears to be ");
    }

    switch (video_type) {
    case VIDEO_H262:
        stream_type = MPEG2_VIDEO_STREAM_TYPE;
        if (!quiet)
            print_msg("MPEG-2 (H.262)\n");
        break;
    case VIDEO_H264:
        stream_type = AVC_VIDEO_STREAM_TYPE;
        if (!quiet)
            print_msg("MPEG-4/AVC (H.264)\n");
        break;
    case VIDEO_AVS:
        stream_type = AVS_VIDEO_STREAM_TYPE;
        if (!quiet)
            print_msg("AVS\n");
        break;
    case VIDEO_UNKNOWN:
        if (!quiet)
            print_msg("Unknown\n");
        print_err("### es2ts: Input video type is not recognised\n");
        close_elementary_stream(&es);
        return 1;
    }

    if (use_stdout)
        err = tswrite_open(TS_W_STDOUT, nullptr, nullptr, 0, quiet, &output);
    else if (use_tcpip)
        err = tswrite_open(TS_W_TCP, output_name, nullptr, port, quiet, &output);
    else
        err = tswrite_open(TS_W_FILE, output_name, nullptr, 0, quiet, &output);
    if (err) {
        close_elementary_stream(&es);
        fprint_err("### es2ts: Unable to open %s\n", output_name);
        return 1;
    }

    if (max2 && !quiet)
        fprint_msg("Stopping after %d ES data units\n", max2);

    err = transfer_data(es, output, pmt_pid, video_pid, stream_type, max2, verbose, quiet);
    if (err)
        print_err("### es2ts: Error transferring data\n");

    close_elementary_stream(&es); // Closes the input file for us
    err2 = tswrite_close(output, quiet);
    if (err2) {
        fprint_err("### es2ts: Error closing output %s: %s\n", output_name, strerror(errno));
        return 1;
    }
    return (err ? 1 : 0);
}
