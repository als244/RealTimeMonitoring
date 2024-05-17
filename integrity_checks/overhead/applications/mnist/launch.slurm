#!/bin/bash
#SBATCH --nodes=1       # one node
#SBATCH --ntasks=1      # numer of tasks across all nodes
#SBATCH --cpus-per-task=8       # cpu cores per task
#SBATCH --gres=gpu:1    # numer of requested GPUS
#SBATCH --mem=80GB	# requested system RAM
#SBATCH --constraint=gpu80	# ensure A100 80GB is used for all timings
# set stdout and stderr
#SBATCH --output=logs/raw_%x_%j.out
#SBATCH --error=logs/raw_%x_%j.err

# send mail
#SBATCH --mail-type=all
#SBATCH --mail-user=as1669@princeton.edu

# Call the program

## now launch application
python mnist_train.py
