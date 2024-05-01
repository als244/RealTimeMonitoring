#!/bin/bash
#SBATCH --nodes=1       # one node
#SBATCH --ntasks=1      # numer of tasks across all nodes
#SBATCH --cpus-per-task=1       # cpu cores per task
#SBATCH --gres=gpu:1    # numer of requested GPUS, NEED TO HAVE A GPU NODE TO TEST
#SBATCH --time=01:00:00
#SBATCH --mem=400G

# set stdout and stderr
#SBATCH --output=%x.%j.out
#SBATCH --error=%x.%j.err


#
# send mail if process fails
#SBATCH --mail-type=all
#SBATCH --mail-user=as1669@princeton.edu
#

# Call the program

./monitor
