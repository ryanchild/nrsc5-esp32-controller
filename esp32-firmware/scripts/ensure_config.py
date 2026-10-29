Import("env")
import os
import shutil

project_dir = env.subst("$PROJECT_DIR")
config_path = os.path.join(project_dir, "include", "config.h")
example_path = os.path.join(project_dir, "include", "config.example.h")

if not os.path.exists(config_path) and os.path.exists(example_path):
    print("*******************************************************************")
    print("* Notice: include/config.h not found!                             *")
    print("* Copying include/config.example.h -> include/config.h            *")
    print("* Edit include/config.h with your Wi-Fi & server URLs to flash.   *")
    print("*******************************************************************")
    shutil.copyfile(example_path, config_path)
