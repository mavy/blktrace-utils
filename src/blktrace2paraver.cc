/**
   blktrace2paraver - Convert a binary blktrace trace to a paraver trace
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
#include <unordered_map>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cstdio>
#include <linux/blktrace_api.h>
#include <unistd.h>

using namespace std;


/* Parameters */
bool COMMS = false; /* Include communications */
bool ENERGY = false;


unsigned int PIDDISK = 1;
unsigned int PIDE5 = 999999+1;
unsigned int PIDE12 = 999999+2;
map < int, string > pid2name;

/* List of events captured */
enum class EVENTS {
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
    ENERGY,
    LAST_ELEMENT
};

enum class TYPES {
    READ = 100000,
    WRITE,
    READSYNC,
    WRITESYNC,
    READMETA,
    WRITEMETA,
    MERGE,
    RA,
    ENERGY5,
    ENERGY12,
    LAST_ELEMENT
};

int numPID;
unsigned long long lastTimeStamp;
map < int, int > PIDS;
map < int, int > RPIDS;

typedef pair < unsigned long long, unsigned long long >P;

typedef map < unsigned int, vector< P > > INFLY_PER_PID;   // MAP with pid, offset and size of a issued operation
unordered_map <  unsigned int , INFLY_PER_PID> INFLY_PER_EVENT;

unordered_map < unsigned int, vector < blk_io_trace > > WANT_SEND;   // We store Insert events...

/* Generates PCF File */
void generatePCFFile(string filename)
{
ofstream PCF;
PCF.open(filename+".pcf");

PCF << "STATES" << endl;
PCF << "0 Completed" << endl;
PCF << "1    ISSUE" << endl;
PCF << "2    DISPATCH" << endl;
PCF << "3    FLUSH" << endl;
PCF << "4    READMETA" << endl;
PCF << "5    WRITEMETA" << endl;
PCF << "6    READSYNC" << endl;
PCF << "7    WRITESYNC" << endl;
PCF << "8    READ" << endl;
PCF << "9    WRITE" << endl;
PCF << "10    DREAD" << endl;
PCF << "11    DWRITE" << endl;
PCF << "12    CREAD" << endl;
PCF << "13    CWRITE" << endl;
PCF << "14    DREADSYNC" << endl;
PCF << "15    DWRITESYNC" << endl;
PCF << "16    CREADSYNC" << endl;
PCF << "17    CWRITESYNC" << endl;
PCF << "18    MERGE" << endl;
PCF << "19    RA" << endl;
PCF << "20    FRONTMERGE" << endl;
PCF << "21    BACKMERGE" << endl;
PCF << "22    ENERGY" << endl;
PCF << "23    LAST_ELEMENT" << endl;

PCF << "DEFAULT_SEMANTIC" << endl;

PCF << "THREAD_FUNC          Last Evt Val" << endl;

PCF << "EVENT_TYPE" << endl;
PCF << "0  100000  READ" << endl;
PCF << "0  100001  WRITE" << endl;
PCF << "0  100002  READSYNC" << endl;
PCF << "0  100003  WRITESYNC" << endl;
PCF << "0  100004  READMETA" << endl;
PCF << "0  100005  WRITEMETA" << endl;
PCF << "0  100006  MERGE" << endl;
PCF << "0  100007  READAHEAD" << endl;
PCF << "0  100008  ENERGY5" << endl;
PCF << "0  100009  ENERGY12" << endl;
PCF << "VALUES" << endl;
PCF << "0 Completed" << endl;
PCF << "1    ISSUE" << endl;
PCF << "2    DISPATCH" << endl;
PCF << "3    FLUSH" << endl;
PCF << "4    READMETA" << endl;
PCF << "5    WRITEMETA" << endl;
PCF << "6    READSYNC" << endl;
PCF << "7    WRITESYNC" << endl;
PCF << "8    READ" << endl;
PCF << "9    WRITE" << endl;
PCF << "10    DREAD" << endl;
PCF << "11    DWRITE" << endl;
PCF << "12    CREAD" << endl;
PCF << "13    CWRITE" << endl;
PCF << "14    DREADSYNC" << endl;
PCF << "15    DWRITESYNC" << endl;
PCF << "16    CREADSYNC" << endl;
PCF << "17    CWRITESYNC" << endl;
PCF << "18    MERGE" << endl;
PCF << "19    READAHEAD" << endl;
PCF << "20    FRONTMERGE" << endl;
PCF << "21    BACKMERGE" << endl;
PCF << "22    LAST_ELEMENT" << endl;

PCF.close();

}


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
            RPIDS[numPID] = trace.pid;
            PIDS[trace.pid] = numPID++;
        }
    }

    int removeInfly ( vector < P > &tmp, P offset)
    {

        int num = 0;

        auto I = tmp.begin();


        unsigned long long obegin = offset.first;
        unsigned long long oend = obegin + offset.second;

        while ( I != tmp.end() )
        {
            unsigned long long sbegin = I->first;
            unsigned long long send = sbegin + I->second;

            if (sbegin >= obegin and send <= oend) {
                I=tmp.erase(I);
                num++;
            }

            else
                ++I;
        }

        return num;
    }

    unsigned long long search_time (vector<blk_io_trace> & ws,bool bdelete)
    {
        unsigned long long result = trace.time;

        auto I=ws.end();
        while (I>ws.begin())
        {
            I--;
            if (I->sector == trace.sector and I->bytes == trace.bytes) //{ delete *it;
                result = I->time;
            if (bdelete) I = ws.erase(I); //}
            return result;
        }

        return result;
    }

    // Converts trace.action to the correct mapped eventid-eventvalue
    void convertEvent (unsigned int & EVENTID, unsigned int &EVENTV)
    {
        int w = trace.action & BLK_TC_ACT(BLK_TC_WRITE);
        int a = trace.action & BLK_TC_ACT(BLK_TC_AHEAD);
        int s = trace.action & BLK_TC_ACT(BLK_TC_SYNC);
        int m = trace.action & BLK_TC_ACT(BLK_TC_META);
        int d = trace.action & BLK_TC_ACT(BLK_TC_DISCARD);
        int f = trace.action & BLK_TC_ACT(BLK_TC_FLUSH);
        int u = trace.action & BLK_TC_ACT(BLK_TC_FUA);

        if (m) {
            if (w)
            {
                EVENTV = static_cast<unsigned int>(EVENTS::WRITEMETA);
                EVENTID = static_cast<unsigned int> (TYPES::WRITEMETA);
            }
            else  {
                EVENTV = static_cast<unsigned int>(EVENTS::READMETA);
                EVENTID = static_cast<unsigned int> (TYPES::READMETA);
            }
        }
        else if (s) {
            if (w)
            {
                EVENTV = static_cast<unsigned int>(EVENTS::WRITESYNC);
                EVENTID = static_cast<unsigned int> (TYPES::WRITESYNC);
            }
            else  {
                EVENTV = static_cast<unsigned int>(EVENTS::READSYNC);
                EVENTID = static_cast<unsigned int> (TYPES::READSYNC);
            }
        }
        else if (w)
        {
            EVENTV = static_cast<unsigned int>(EVENTS::WRITE);
            EVENTID = static_cast<unsigned int> (TYPES::WRITE);
        }
        else  {
            EVENTV = static_cast<unsigned int>(EVENTS::READ);
            EVENTID = static_cast<unsigned int> (TYPES::READ);
        }
    }

    /* Converts the trace line to a prv event */
    void toPRV (ofstream & PAR) {
        if (trace.pdu_len == 0) { /* Only count if is a std trace line */
            // ISSUE
            lastTimeStamp =  trace.time;
            int action = trace.action & 0xffff;

            unsigned int EVENTV = 0;
            unsigned int EVENTID = 0;
            switch (action) {
            case __BLK_TA_COMPLETE:
            {

                convertEvent(EVENTID, EVENTV);
                // We need to insert as many completes as infly operations we have
                auto I = INFLY_PER_EVENT[EVENTID].begin();

                while ( I != INFLY_PER_EVENT[EVENTID].end())
                {
                    // I -> is a map per pid
                    /* MPID es el map < pid, vector > */
                    int num = removeInfly(I->second, P(trace.sector, trace.bytes)); // paso el vector
                    EVENTV = static_cast<unsigned int>(EVENTS::COMPLETE);
                    if ( PIDS.find(I->first) == PIDS.end() ) cout << "Not exists " << I->first << endl;
                    for (int i = 0; i<num; i++)
                    {
                        PAR << "2:" << trace.cpu+1 << ":1:1:" << PIDS[I->first] << ":" << (unsigned long long) (trace.time) << ":" << EVENTID << ":" << EVENTV << endl;
                    
                        if (COMMS and I->first != PIDDISK) PAR << "3:"
                        << trace.cpu+1 << ":1:1:" << PIDDISK << ":" << (unsigned long long) (trace.time) << ":"  << trace.time << ":"
                        << trace.cpu+1 << ":1:1:" << PIDS[I->first] << ":" << (unsigned long long) (trace.time) << ":"  << trace.time << ":" << trace.bytes << ":" << trace.sector << endl;
                      }
                    ++I;
                }
                PAR << "2:" << trace.cpu+1 << ":1:1:" << PIDS[trace.pid] << ":" << (unsigned long long) (trace.time) << ":" << EVENTID << ":" << EVENTV << endl;

            }
            break;

            case __BLK_TA_INSERT:

            {
                convertEvent(EVENTID, EVENTV);

                PAR << "2:" << trace.cpu+1 << ":1:1:" << PIDS[trace.pid] << ":" << (unsigned long long) (trace.time) << ":" << EVENTID << ":" << EVENTV << endl;
                INFLY_PER_EVENT[EVENTID][trace.pid].push_back( P (trace.sector, trace.bytes) );
                if (COMMS) WANT_SEND[trace.pid].push_back (trace);
            }
            break;

            case __BLK_TA_ISSUE:
                /* Issued are presented into the virtual disk layer -- PID 1
                  We also put them on the infly_per_event to be able to use a stacked val function (need to check)
                */
            {
                convertEvent(EVENTID, EVENTV);

                PAR << "2:" << trace.cpu+1 << ":1:1:" << PIDDISK << ":" << (unsigned long long) (trace.time) << ":" << EVENTID << ":" << EVENTV << endl;
                INFLY_PER_EVENT[EVENTID][1].push_back( P (trace.sector, trace.bytes) );
                
                unsigned long long originalsendTime = search_time (WANT_SEND[trace.pid],true);

                /* Generate communication line */
                if (COMMS) PAR << "3:" << trace.cpu+1 << ":1:1:" << PIDS[trace.pid] << ":" << (unsigned long long) (originalsendTime) << ":"
                    << (unsigned long long) ( trace.time ) << ":"
                    << trace.cpu+1 << ":1:1:" << PIDDISK << ":" <<  (unsigned long long) trace.time << ":"
                    << (unsigned long long) (trace.time ) << ":"
                    << trace.bytes << ":" << trace.sector << endl;
            }
            break;
            case __BLK_TA_BACKMERGE :


            case __BLK_TA_FRONTMERGE :

                EVENTV = static_cast<unsigned int>(EVENTS::MERGE);
                EVENTID = static_cast<unsigned int> (TYPES::MERGE);

                PAR << "2:" << trace.cpu+1 << ":1:1:" << PIDS[trace.pid] << ":" << (unsigned long long) (trace.time) << ":" << EVENTID << ":" << EVENTV << endl;
                break;

            case __BLK_TA_QUEUE:
                if (trace.action & BLK_TC_ACT(BLK_TC_AHEAD)) {
                    EVENTV = static_cast<unsigned int>(EVENTS::RA);
                    EVENTID = static_cast<unsigned int> (TYPES::RA);

                    PAR << "2:" << trace.cpu+1 << ":1:1:" << PIDS[trace.pid] << ":" << (unsigned long long) (trace.time) << ":" << EVENTID << ":" << EVENTV << endl;
                }
                break;
           }
        }
    }
};



string ofilename = "";
string efilename = "";
int
main (int argc, char **argv)
{
    string ifilename;

    if (argc < 2)  {
        cerr << "Ramon Nou @ Barcelona Supercomputing Center" << endl << "Usage: blktrace2parever -i <inputbinarytrace> -o <outputtracename> -c (include comms) -e <energy>" << endl;
        exit(-1);
    }

    int opterr = 0;
    int c;

    while ((c = getopt (argc, argv, "i:o:ce:")) != -1)
        switch (c) {
        case 'i':
            ifilename = optarg;
            break;

        case 'o':
            ofilename = optarg;
            break;
        case 'c':
            COMMS = true;
            break;
	case 'e':
            ENERGY = true;
	    efilename = optarg;
	    break;

        case '?':
            cerr << "Ramon Nou @ Barcelona Supercomputing Center" << endl << "Usage: blktrace2parever -i <inputbinarytrace> -o <outputtracename> -c (include comms)" << endl;

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

    ofstream PAR;
    PAR.open ("tmp.prv");
   
    string linea;
    double stime = -1;

    ifstream ifs;
    ifs.open (ifilename.c_str(), std::ifstream::binary);

    if (!(ifs.is_open() and ifs.good())) {
        cerr << "We have some problem with the input file, check " << endl;
        exit(-1);
    }
    
    ifstream efs;

    if (ENERGY) {
	efs.open (efilename.c_str());
        if (!(efs.is_open() and efs.good())) {
        cerr << "We have some problem with the energy file, check " << endl;
        exit(-1);
        }
    }

	
    blk_io_trace trace;

    
    // We generate a Virtual "thread" that simulates the disk activity
    numPID = 4;
    PIDS[1] = 1; 
    RPIDS[1] = 1;
    pid2name[1] = "Disk";
    
    PIDS[999999+1] = 2;
    RPIDS[2] = 999999+1;
    pid2name[999999+1] = "Energy - Logic";

    PIDS[999999+2] = 3;
    RPIDS[3] = 999999+2;
    pid2name[999999+2] = "Energy - Mech";

    while ((ifs.is_open () and ifs.good ())) {
        ifs.read ((char *) &trace, sizeof (blk_io_trace));
        traceLine linea (trace, ifs);
        linea.toPRV (PAR);
    }

    ifs.close ();

    if (ENERGY)
    {
	double initial = -1;
        while ((efs.is_open () and efs.good ())) {
           double ts;
    	   unsigned int ma5, ma12;
           efs >> ts >> ma5 >> ma12;
	   if ( initial < 0) initial = ts;
        
       unsigned int   EVENTID = static_cast<unsigned int> (TYPES::ENERGY5);

	PAR << "2:" << 1 << ":1:1:" << PIDS[PIDE5] << ":" << (unsigned long long) ((ts-initial)*1000000000.0) << ":" << EVENTID << ":" << ma5 << endl;
    EVENTID = static_cast<unsigned int> (TYPES::ENERGY12);

    PAR << "2:" << 1 << ":1:1:" <<  PIDS[PIDE12] << ":" << (unsigned long long) ((ts-initial)*1000000000.0) << ":" << EVENTID << ":" << ma12 << endl;
    }
	efs.close();


    }

    // Generacion del fichero de nombres (ROW)
    ofstream ROW;
    ROW.open (ofilename+".row");
    ROW << "LEVEL THREAD SIZE " << numPID << endl;


    for (int i = 1; i < numPID; i++)
    {
        if (RPIDS.find (i) == RPIDS.end ())
            ROW << "PID " << i << endl;
        else
            ROW << pid2name[ RPIDS[i] ] << endl;
    }


    ROW.close ();
    PAR.close ();

    PAR.open (ofilename+".prv");
    // Number of cores is hardcoded (as 8)
    PAR << "#Paraver (06/08/14 at 23:30):"<<lastTimeStamp<<":1(8):1:1(" << numPID-1 << ":1)" << endl;
   
    generatePCFFile (ofilename);

    std::ifstream TRACE("tmp.prv", std::ios_base::binary);
    PAR << TRACE.rdbuf();
    PAR.close();
    TRACE.close();
    remove("tmp.prv");
}
