************************************************************************
file with basedata            : cm239_.bas
initial value random generator: 691675504
************************************************************************
projects                      :  1
jobs (incl. supersource/sink ):  18
horizon                       :  127
RESOURCES
  - renewable                 :  2   R
  - nonrenewable              :  2   N
  - doubly constrained        :  0   D
************************************************************************
PROJECT INFORMATION:
pronr.  #jobs rel.date duedate tardcost  MPM-Time
    1     16      0       29        0       29
************************************************************************
PRECEDENCE RELATIONS:
jobnr.    #modes  #successors   successors
   1        1          3           2   3   4
   2        2          3           5  13  14
   3        2          3           6  10  11
   4        2          3           7  12  15
   5        2          3          10  12  17
   6        2          3           7   8   9
   7        2          2          13  16
   8        2          2          12  14
   9        2          2          13  14
  10        2          2          15  16
  11        2          1          17
  12        2          1          16
  13        2          1          17
  14        2          1          15
  15        2          1          18
  16        2          1          18
  17        2          1          18
  18        1          0        
************************************************************************
REQUESTS/DURATIONS:
jobnr. mode duration  R 1  R 2  N 1  N 2
------------------------------------------------------------------------
  1      1     0       0    0    0    0
  2      1     2       8    9    9    5
         2     9       7    7    7    3
  3      1     8       7    4    7    5
         2    10       5    2    6    5
  4      1     4      10    5    9    7
         2    10       7    5    7    5
  5      1     5       8   10    8    9
         2     9       4   10    8    2
  6      1     4      10    5    4   10
         2     8       6    5    4    6
  7      1     3       7    4    8    8
         2     5       6    3    8    7
  8      1     2       7    7    9    7
         2     9       7    4    7    5
  9      1     9       5    7    2    5
         2     9       2    6    1   10
 10      1     3       8    3    8    5
         2     7       8    1    8    4
 11      1     6       9    8    3    3
         2     8       8    6    2    2
 12      1     2       2    9   10    9
         2     2       4    9    5    8
 13      1     5       3    7   10    8
         2     8       2    5   10    8
 14      1     6       3    6    8    9
         2     9       3    4    4    7
 15      1     1       6    2    6    5
         2    10       4    1    5    1
 16      1     9       6    7    6   10
         2    10       4    5    6   10
 17      1     3       8    9   10    5
         2     4       6    8    7    4
 18      1     0       0    0    0    0
************************************************************************
RESOURCEAVAILABILITIES:
  R 1  R 2  N 1  N 2
   23   22  101   90
************************************************************************