#!/usr/local/bin/bash

usage() {
    echo -e "\nUsage:\n test_run.sh <routes> <mat> [days] [processes] [reps]\n" 
    exit 1
}

if [  $# -le 1 ]
then
	usage
fi

# Make a dir for nodes to dump data to
mkdir -p out_dat

MAXDIFF=4


# Parse all the args
ROUTES=$1
MAT=$2

if [ "$#" -eq 2 ]; then
	DAYS=1
	PROCS=4
	REPS=1
elif [ "$#" -eq 3 ]; then
	DAYS=$3
	PROCS=4
	REPS=1
elif [ "$#" -eq 4 ]; then
	DAYS=$3
	PROCS=$4
	REPS=1
elif [ "$#" -eq 5 ]; then
	DAYS=$3
	PROCS=$4
	REPS=$5
else
	usage
fi

MAXTS=`expr 86400 \* $DAYS`

echo "Executing for $MAXTS ($DAYS days) on $PROCS proccess with $REPS optimistic reps..."

# Clean up leftovers
rm -f base.out seq.raw seq.out con.raw con.out opt.raw opt.out

# Base run
#models/capacity/src/capacity --synch=1 --mat=$MAT --routes=$ROUTES --end=1382400> base.raw
#cat base.raw | grep \\\[ | sort > base.out

## Run it sequential
echo "Running Sequential.."
mpirun -np 1 models/capacity/src/capacity --synch=1 --mat=$MAT --routes=$ROUTES --end=$MAXTS > seq.raw
# Filter for messages that start with the bracket
# Filter out the lines with run info from ross
# Sort by the time stamp in the brackets
cat seq.raw | grep \\\[ | grep -v "Total KPs" | sort -n -t[ -k2 > seq.out

echo "Running Conservative..."
mpirun -np $PROCS models/capacity/src/capacity --synch=2 --mat=$MAT --routes=$ROUTES --end=$MAXTS  > con.raw
cat con.raw | grep \\\[ | grep -v "Total KPs" | sort -n -t[ -k2 > con.out

# Compare them
diff seq.out con.out

if [ $? -eq 1 ]
then
    echo "Sequential and Conservative runs did not match!"
    exit 1
fi

echo "Running Optimistic..."
# Run it optimistic, lots of times
for i in $(seq $REPS)
    do
        echo -e "\t$i..."
        mpirun -np $PROCS models/capacity/src/capacity --synch=3 --mat=$MAT --routes=$ROUTES --end=$MAXTS --extramem=1024 > opt.raw
        # break out if it fails
        if [ "$?" -ne 0 ]
        then
            echo "Failed!"
            exit 1
            break
        fi
        cat opt.raw | grep \\\[ | grep -v "Total KPs" | sort -n -t[ -k2 > opt.out

        # Compare it it to the seq/con
        diff con.out opt.out
        if [ $? -eq 1 ]
        then
            echo "Optimistic and Conservative mode did not match!"
            exit 1
        fi
    done


# Clean up leftovers
rm -f base.out seq.raw seq.out con.raw con.out opt.raw opt.out
