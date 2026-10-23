import os
from SCons.Script import DefaultEnvironment
import shutil

env = DefaultEnvironment()

# def after_upload(source, target, env):
#     print("Uploading LittleFS image...")
#     os.system("pio run -t uploadfs")
    

# env.AddPostAction("upload", after_upload)


LOG_FILES = ["gen2-log.bin", "pointers.bin"]  # List your log files here
LITTLEFS_DIR = "data"
BACKUP_DIR = os.path.join(LITTLEFS_DIR, "backup")


def restore_logs():
    # Restore log files
    for log_file in LOG_FILES:
        src = os.path.join(BACKUP_DIR, log_file)
        dst = os.path.join(LITTLEFS_DIR, log_file)
        if os.path.exists(src):
            shutil.move(src, dst)
            print(f"Restored {src} to {dst}")


def delete_backup():
    # Delete backup directory and its contents
    if os.path.exists(BACKUP_DIR):
        shutil.rmtree(BACKUP_DIR)
        print(f"Deleted backup directory {BACKUP_DIR}")
              
def after_upload(source, target, env):
    print("Uploading LittleFS image...")

    os.system("pio run -t uploadfs")
    print("Deleting backup directory...")
    delete_backup()
    

env.AddPostAction("upload", after_upload)