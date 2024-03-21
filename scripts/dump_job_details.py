import os
import sys
from datetime import date
import csv
import sqlite3
import time

def main(args):

    if len(args) < 2:
        ### PASS HOSTNAME AS COMMAND LINE ARG
        hostname = os.uname()[1]
    else:
        hostname = args[1]

    ## set up dumping to db
    db_path = "/scratch/gpfs/as1669/ClusterMonitoring/data/job_details/" + hostname + "_jobs.db"
    table_name = "Jobs"
    con = sqlite3.connect(db_path)
    cur = con.cursor()
    
    ## create table if not exists
    table_creation = """ CREATE TABLE IF NOT EXISTS Jobs (
                             job_id INT,
                             user_id VARCHAR(10),
                             group_id VARCHAR(20),
                             n_nodes INT,
                             n_cpus INT,
                             n_gpus INT,
                             mem_mb INT,
                             billing INT,
                             time_limit CHAR(8),
                             submit_time CHAR(19),
                             node_list VARCHAR(255),
                             start_time CHAR(19),
                             end_time CHAR(19),
                             elapsed_time CHAR(8),
                             state VARCHAR(20),
                             exit_code CHAR(3),
                             PRIMARY KEY (job_id)
                             ); """

    cur.execute(table_creation)
    con.commit()


    ### creating file to dump sacct to 
    time_sec = round(time.time())
    raw_data_dir = "/scratch/gpfs/as1669/ClusterMonitoring/data/job_details/temp/"
    sacct_file_name = raw_data_dir + hostname + "_" + str(time_sec) + ".out"

    ### call slurm sacct command to retrieve data
    ### assume this will be called 1once per hour!
    sacct_cmd = "sacct --nodelist={hostname} --format=User,Group,JobID,ReqTRES,Timelimit,Submit,NodeList,Start,End,Elapsed,State,ExitCode --state=COMPLETED,CANCELLED,FAILED,TIMEOUT,OUT_OF_MEMORY --starttime=now-1hour --endtime=now --unit=M --allusers -P".format(hostname=hostname)
    redirect_cmd = " > {outfile}".format(outfile=sacct_file_name)
    system_cmd = sacct_cmd + redirect_cmd
    ## run system command
    os.system(system_cmd)

    
    ## now parse the output and upload to sqlite DB
    
    with open(sacct_file_name, newline='') as sacct_file:
    
        sacct_reader = csv.DictReader(sacct_file, delimiter="|")
        cnt = 0
        for row in sacct_reader:
            user, group = row['User'], row['Group']
            ## additional rows with .batch .execute will not have user and we dont need those rows
            if len(user) == 0:
                continue

            job_id = int(row['JobID'])
            ### parse the ReqTRES info to get GPU request
            req_info = {}
            raw_gpu_req_arr = row['ReqTRES'].split(",")
            for item in raw_gpu_req_arr:
                k, v = item.split("=")
                req_info[k] = v
        
            ## for all jobs
            n_nodes = int(req_info["node"])
            n_cpus = int(req_info["cpu"])
            ## last character is the unit (M)
            mem_mb = int(req_info["mem"][:-1])
            billing = int(req_info["billing"])
            ## jobs with gpu
            n_gpus = int(req_info["gres/gpu"]) if "gres/gpu" in req_info else 0

            ## get timelimit and submit time info
        
                ## format HH:MM:SS
            time_limit = row["Timelimit"]
        
                ## format <YYYY-MM-DD>T<HH:MM:DD> 
            submit_time = row["Submit"]

            ## get list of allocated nodes
            ## comma-delimited list of hostnames
            node_list = row["NodeList"]

            ## get the start,end,elapsed times
            
                ## formats <YYYY-MM-DD>T<HH:MM:DD>
            start_time = row["Start"]
            end_time = row["End"]

                ## format HH:MM:SS
            elapsed = row["Elapsed"]


            ## get state info (COMPLETED,CANCELLED,etc.)
            state = row["State"]

            ## get exit code
            ## format exit_code:signal
            exit_code = row["ExitCode"]

            ### INSERT INTO DB
            insert_cmd = """INSERT INTO Jobs
                        (job_id, user_id, group_id, n_nodes, n_cpus, n_gpus, mem_mb, billing, time_limit, submit_time, node_list, start_time, end_time, elapsed_time, state, exit_code)
                        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"""

            data_values = (job_id, user, group, n_nodes, n_cpus, n_gpus, mem_mb, billing, time_limit, submit_time, node_list, start_time, end_time, elapsed, state, exit_code)
        
            ## do insertion
            cur.execute(insert_cmd, data_values)
            ## commit per row
            con.commit()
    
    ## close the db
    cur.close()
    con.close()
    
    ## delete file used for sacct data
    os.remove(sacct_file_name)


if __name__ == "__main__":
    
    main(sys.argv)

