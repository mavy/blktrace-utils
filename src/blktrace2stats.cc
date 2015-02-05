/**
   blktrace2stats - Extract stats from blktrace output files
   Copyright (C) 2014 Ramon Nou at Barcelona Supercomputing Center

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
   */

/*
   Expected input is a binary file generated from blkparse -d <file> -i trace,
   blkparse sorts and processes the different input files per CPU.

   We do not filter per device, or process remap actions
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <map>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cstdio>
#include <linux/blktrace_api.h>
#include <unistd.h>
using namespace std;

map < int, string > pid2name;

/* List of events captured */
enum EVENTS {
    COMPLETE = 0,
    ISSUE,
    DISPATCH,
    FLUSH,
    READMETA, WRITEMETA,
    READSYNC,
    WRITESYNC,
    READ,
    WRITE,
    DREAD,
    DWRITE,
    CREAD,
    CWRITE, DREADSYNC,
    DWRITESYNC,
    CREADSYNC,
    CWRITESYNC,
    MERGE,
    RA,
    FRONTMERGE,
    BACKMERGE,
    LAST_ELEMENT
};

/* counts events per pid */
typedef vector < unsigned int >COUNT;
map < int, COUNT > mCOUNT;

/*
   traceLine is a basic class to process a trace line from blktrace.
   If the trace line includes a payload (used by blktrace to output process names),
   we automatically read it.

   However, it does not support remap actions, but they are not important.
 */

class traceLine
{
private:
    blk_io_trace trace;
public:
    traceLine (const struct blk_io_trace &tr, ifstream & ifs) {
        memcpy (&trace, &tr, sizeof (blk_io_trace));
        char pName[trace.pdu_len];

        if (trace.pdu_len > 0 ) {
            ifs.read ((char *) &pName, trace.pdu_len);
        }

        // Additional data is the name of the process or a remap action (not processed)
        if (trace.action == BLK_TN_PROCESS) {
            pid2name[trace.pid] = pName;
        }
    }

    /* Fills the data needed to count events */
    void count (map < int, COUNT > &mC) {
        if (mC.find (trace.pid) == mC.end ()) {
            COUNT tc = COUNT (LAST_ELEMENT, 0); /* Inits counting structure for pid */
            mC[trace.pid] = tc;
        }
        else if (trace.pdu_len == 0) { /* Only count if is a std trace line */
            // ISSUE
            COUNT & tC = mC[trace.pid];
            int action = trace.action & 0xffff;
            int w = trace.action & BLK_TC_ACT(BLK_TC_WRITE);
            int a = trace.action & BLK_TC_ACT(BLK_TC_AHEAD);
            int s = trace.action & BLK_TC_ACT(BLK_TC_SYNC);
            int m = trace.action & BLK_TC_ACT(BLK_TC_META);
            int d = trace.action & BLK_TC_ACT(BLK_TC_DISCARD);
            int f = trace.action & BLK_TC_ACT(BLK_TC_FLUSH);
            int u = trace.action & BLK_TC_ACT(BLK_TC_FUA);

            switch (action) {
            case __BLK_TA_COMPLETE:
                tC[COMPLETE]++;
                

                if (s) {
                    if (w) tC[CWRITESYNC]++;    // Writes/READS on I
                    else tC[CREADSYNC]++;
                }
                else if (w) tC[CWRITE]++;
                else tC[CREAD]++;  // Writes/READS on I

                break;

            case __BLK_TA_ISSUE:
                tC[DISPATCH]++;

                if(m) {
                    if(w) tC[WRITEMETA]++;
                    else tC[READMETA]++;
                }
                else if (s) {
                    if (w) tC[DWRITESYNC]++;    // Writes/READS on I
                    else tC[DREADSYNC]++;
                }
                else if (w) tC[DWRITE]++;
                else tC[DREAD]++;  // Writes/READS on I

                break;

            case __BLK_TA_INSERT:
                tC[ISSUE]++;

                if (s) {
                    if (w) tC[WRITESYNC]++;    // Writes/READS on I
                    else tC[READSYNC]++;
                }
                else if (w) tC[WRITE]++;
                else tC[READ]++;  // Writes/READS on I

                break;

            case __BLK_TA_BACKMERGE :
                tC[MERGE]++;
                break;

            case __BLK_TA_FRONTMERGE :
                tC[MERGE]++;
                break;

             case __BLK_TA_QUEUE:
                if (a) tC[RA]++;
                break;   
            }
        }
    }
};

/* Output WIKI formatted stats */
void printWIKI (const map <int, COUNT> & mC, bool compact)
{
    if (compact)
        cout << "{|border=\"1\"" << endl <<
             "!Process||PID||RM||WM||R||RS||W||WS||RA||M||I||D||C" << endl <<
             "|- align=\"right\" " << endl;
    else
        cout << "{|border=\"1\"" << endl <<
             "!Process||PID||RM||WM||IR||IRS||DR||DRS||CR||CRS||IW||IWS||DW||DWS||CW||CWS||RA||M||I||D||C" << endl <<
             "|- align=\"right\" " << endl;

    for (auto I  : mC) {
        const COUNT c = I.second;
        string s = "||";
        if (compact) s = "/";
            cout << "|" << pid2name[I.first] << "||" << I.first << "||";
            cout << c[READMETA] << "||" <<c[WRITEMETA] << "||";
            cout << c[READ] << s << c[DREAD] << s << c[CREAD] << "||" << c[READSYNC] << s << c[DREADSYNC] << s << c[CREADSYNC] << "||";
            cout << c[WRITE] << s << c[DWRITE] << s << c[CWRITE] << "||" << c[WRITESYNC] << s << c[DWRITESYNC] << s << c[CWRITESYNC] << "||";
            cout << c[RA] << "||" << c[MERGE] << "||" ;
            cout << c[ISSUE] << "||" << c[DISPATCH] << "||" << c[COMPLETE]  << endl <<
                 "|- align=\"right\"" << endl;
    }

    cout << "}" << endl;
}


string format(unsigned int value, int W)
{
    string output = to_string(value);

    if (output.length() >= W ) {
        value /= 1000;
        output = to_string(value);
        output += "K";
    }

    return output;
}
/* Output TABBED formatted stats */
void printTABBED (const map <int, COUNT> & mC, bool compact, int WIDTH)
{
    int W = WIDTH;
    cout << setw (16) << "Process" << setw (W) << "PID" ;
    cout << setw(W) << "RMD" << setw(W) << "WMD" ;

    if (compact)
        cout << setw(W * 3) << "R" << setw(W * 3) << "RS";
    else {
        cout << setw (W) << "IR" <<  setw (W) << "IRS";
        cout << setw (W) << "DR" << setw (W) << "DRS";
        cout << setw (W) << "CR" << setw (W) << "CRS";
    }

    if (compact) {
        cout << setw(W * 3) << "W" << setw(W * 3) << "WS";
    }
    else {
        cout << setw (W) << "IW" <<  setw (W) << "IWS";
        cout << setw (W) << "DW" << setw (W) << "DWS";
        cout << setw (W) << "CW" << setw (W) << "CWS";
    }

    cout << setw (W) << "RA" << setw (W) << "M";
    cout << setw (W) << "I" << setw (W) << "D" << setw (W) << "C" << endl;

    for (auto I  : mC) {
        const COUNT c = I.second;
        cout << setw (16) << pid2name[I.first] << setw (W) << I.first <<    setw (W) << format(c[READMETA], W) << setw (W) << format(c[WRITEMETA], W);

        if (compact) {
            cout << setw (W * 3) << (format(c[READ], W) + "/" + format(c[DREAD], W) + "/" + format(c[CREAD], W));
            cout << setw (W * 3) << (format(c[READSYNC], W) + "/" + format(c[DREADSYNC], W) + "/" + format(c[CREADSYNC], W));
        }
        else {
            cout << setw (W) << format(c[READ], W) << setw (W) << format(c[READSYNC], W) <<
                 setw (W) << format(c[DREAD], W) << setw (W) << format(c[DREADSYNC], W) <<
                 setw (W) << format(c[CREAD], W) << setw (W) << format(c[CREADSYNC], W) ;
        }

        if (compact) {
            cout << setw (W * 3) << (format(c[WRITE], W) + "/" + format(c[DWRITE], W) + "/" + format(c[CWRITE], W));
            cout << setw (W * 3) << (format(c[WRITESYNC], W) + "/" + format(c[DWRITESYNC], W) + "/" + format(c[CWRITESYNC], W));
        }
        else {
            cout << setw (W) << format(c[WRITE], W) << setw (W) << format(c[WRITESYNC], W) <<
                 setw (W) << format(c[DWRITE], W) << setw (W) << format(c[DWRITESYNC], W) <<
                 setw (W) << format(c[CWRITE], W) << setw (W) << format(c[CWRITESYNC], W) ;
        }

        cout <<  setw (W) << format(c[RA], W) <<
             setw (W) << format(c[MERGE], W) << setw (W) << format(c[ISSUE], W) <<
             setw (W) << format(c[DISPATCH], W) << setw (W) <<
             format(c[COMPLETE], W)  << endl;
    }
}

int main (int argc, char **argv)
{
    bool WIKI = false;
    bool COMPACT = false;
    int WIDTH = 5;
    string filename;

    if (argc < 2)  {
        cerr << "Ramon Nou @ Barcelona Supercomputing Center" << endl << "Usage: blktrace2stats -i <inputbinarytrace> -w (wiki output) -c (compact output) -W <width>" << endl;
        exit(-1);
    }

    int opterr = 0;
    int c;

    while ((c = getopt (argc, argv, "i:wcW:")) != -1)
        switch (c) {
        case 'i':
            filename = optarg;
            break;

        case 'w':
            WIKI = true;
            break;

        case 'W':
            WIDTH = stoi ((string)optarg);
            break;

        case 'c':
            COMPACT = true;
            break;

        case '?':
            cerr << "Ramon Nou @ Barcelona Supercomputing Center" << endl << "Usage: blktrace2stats -i <inputbinarytrace> -w (wiki output) -c (compact output)]" << endl;

            if (optopt == 'i')
                fprintf (stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
                fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf (stderr,
                         "Unknown option character `\\x%x'.\n",
                         optopt);

            return 1;

        default:
            abort ();
        }

    string linea;
    ifstream ifs;
    ifs.open (filename.c_str(), std::ifstream::binary);

    if (!(ifs.is_open() and ifs.good())) {
        cerr << "We have some problem with the input file, check " << endl;
        exit(-1);
    }

    blk_io_trace trace;

    while ((ifs.is_open () and ifs.good ())) {
        ifs.read ((char *) &trace, sizeof (blk_io_trace));
        traceLine linea (trace, ifs);
        linea.count (mCOUNT);
    }

    ifs.close ();

    if (WIKI) printWIKI(mCOUNT,COMPACT);
    else printTABBED(mCOUNT, COMPACT, WIDTH);
}
