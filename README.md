# blktrace-utils
Some utils for blktrace binary output, including a translator to paraver trace format: [Paraver at BSC](http://www.bsc.es/computer-sciences/performance-tools/paraver)

These utilities were created for IOLanes EU project to analyze what was happening in the I/O Stack. 

## Author
Ramon Nou @ Barcelona Supercomputing Center (2014)

## Installation / Compilation

`> ./configure`

`> make`

## Input file

The input file of the utilities is a binary trace generated with blktrace.

Example (you may need root to generate traces with blkltrace):

` > blktrace -o trace -d /dev/sda`

Stop the trace generation with ctrl+c

` > blkparse -d bp.bin -i ~/trace -O`


## blktrace2stats

Using a blktrace trace as basis, we can extract basic statistics about operations (merges, number of reads, syncs) that help to analyze the changes done in the filesystem. For example, if the I/O is better aligned we will have more merges, reducing the number of requests going to the disk driver.

### Usage: 
`> blktrace2stats -i <inputbinarytrace> -w (wiki output) -c (compact output) -W <width>`

####Options

- `-i <inputbinarytrace>` is a blktrace trace, parsed using blkparse: `blkparse -d <binarytrace> -i <trace>`

- `-w`: Optional, activates mediawiki table output
- `-c`: (Optional) Activates compacted format, Reads from the different layers (Issue, Dispatch, Complete) will be separated by a '/' instead of a tab
- `-W <width>`: (Optional) Specifies the width of the columns. If the number does not fits the width, it will be rounded to K units.

### Considerations

The 0 process, follows the standard semantics of blktrace. Nearly all the completions are marked with the 0 process (root process). The process name is the last one, but it should not be considered as the only one.

 ReadAheads are counted on the queue event, as they are converted to standard reads when they enter the scheduler.

### Example:
    
    Process  PID  RMD  WMD              R             RS              W             WS   RA    M    I    D    C
    hexdump 7306   70   53 7288/7168/2196          0/0/0        0/2/113         0/3/56 2308   24 7288 7296 2365
   
    MD=Metadata, R = READ, W = WRITE, S = SYNC,  M = Merge, RA=Read Ahead, I = Send to Queues, D = Send to Driver, C = Complete,

On this example we can see how the number of request completed returning from the disk are low compared to the dispatched ones, merges are low so it means that the merges are done at the disk level.

## blktrace2prv

Using a blktrace trace as basis, we can extract basic statistics about operations (merges, number of reads, syncs) that help to analyze the changes done in the filesystem. For example, if the I/O is better aligned we will have more merges, reducing the number of requests going to the disk driver.

### Usage: 
`> blktrace2stats -i <inputbinarytrace> -w (wiki output) -c (compact output) -W <width>`

####Options

- `-i <inputbinarytrace>` is a blktrace trace, parsed using blkparse: `blkparse -d <binarytrace> -i <trace>`

- `-w`: Optional, activates mediawiki table output
- `-c`: (Optional) Activates compacted format, Reads from the different layers (Issue, Dispatch, Complete) will be separated by a '/' instead of a tab
- `-W <width>`: (Optional) Specifies the width of the columns. If the number does not fits the width, it will be rounded to K units.

### Considerations

The 0 process, follows the standard semantics of blktrace. Nearly all the completions are marked with the 0 process (root process). The process name is the last one, but it should not be considered as the only one.

 ReadAheads are counted on the queue event, as they are converted to standard reads when they enter the scheduler.

### Example:
    
    Process  PID  RMD  WMD              R             RS              W             WS   RA    M    I    D    C
    hexdump 7306   70   53 7288/7168/2196          0/0/0        0/2/113         0/3/56 2308   24 7288 7296 2365
   
    MD=Metadata, R = READ, W = WRITE, S = SYNC,  M = Merge, RA=Read Ahead, I = Send to Queues, D = Send to Driver, C = Complete,

On this example we can see how the number of request completed returning from the disk are low compared to the dispatched ones, merges are low so it means that the merges are done at the disk level.


    

