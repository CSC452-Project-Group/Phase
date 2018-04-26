#!/bin/ksh
dir=/home/cs452/spring18/phase5/testResults
#dir=/home/cs452/spring18/admin/project/phase5/testResults
#dir=/Users/patrick/Classes/452/project/phase5/testResults

if [ "$#" -eq 0 ] 
then
    echo "Usage: ksh testphase5.ksh <num>"
    echo "where <num> is 1, 2, ... or 8"
    exit 1
fi

num=$1
if [ -f test${num} ]
then
    /bin/rm test${num}
fi

# Copy disk files
#    Do not need terminal files for phase 5
cp testcases/disk0.orig disk0
cp testcases/disk1.orig disk1

if  make test${num} 
then

    ./test${num} > test${num}.txt 2> test${num}stderr.txt;

    if [ -s test${num}stderr.txt ]
    then
        cat test${num}stderr.txt >> test${num}.txt
    fi

    /bin/rm test${num}stderr.txt

#    if [ "${num}" -eq 14 ]; then
#        cmp disk1 testResults/disk14
#        diskCompare=$?
#        echo "diskCompare = "${diskCompare}
#        if diff --brief test${num}.txt ${dir}
#        then
#            if [ ${diskCompare} -eq 0 ] ; then
#                echo
#                echo test${num} passed!
#            else
#                echo
#                echo test${num} failed!
#                echo incorrect contents in disk1
#            fi
#        else
#            echo
#            diff -C 1 test${num}.txt ${dir}
#        fi
#        exit
#    fi

    if diff --brief test${num}.txt ${dir}
    then
        echo
        echo test${num} passed!
    else
        echo
        diff -C 1 test${num}.txt ${dir}
    fi
fi
echo
