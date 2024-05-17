#!/bin/bash
#SBATCH --nodes=1	# one node
#SBATCH --ntasks=1	# numer of tasks across all nodes
#SBATCH --cpus-per-task=1	# cpu cores per task
#SBATCH --gres=gpu:1 	# numer of requested GPUS
#SBATCH --account=pvl	# account to run program
#SBATCH --partition=pvl # partition in which to find node
#SBATCH --nodelist=node101	# specific node to run on
#SBATCH --time=03:00:00		# total run time limit
#
# send mail if process fails
#SBATCH --mail-type=ALL
#SBATCH --mail-user=as1669@princeton.edu
#

# Call the program

./ComputeTest
