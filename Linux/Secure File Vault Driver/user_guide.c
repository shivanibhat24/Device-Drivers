ExportPublishSecure Vault Driver - Usage Guide
This guide demonstrates how to use the Secure Vault Driver to create secure files that are only accessible to specific processes, groups, or sessions.
Compilation and Installation
First, let's compile and install the driver:
bash# Save the code as secure_vault_driver.c
# Create a Makefile
echo 'obj-m += secure_vault_driver.o' > Makefile
echo 'all:' >> Makefile
echo '	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules' >> Makefile
echo 'clean:' >> Makefile
echo '	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean' >> Makefile

# Compile the module
make

# Install the module
sudo insmod secure_vault_driver.ko
Basic Usage
After loading the module, you'll find a new directory in DebugFS:
bash# Check if debugfs is mounted
mount | grep debugfs

# If not mounted, mount it
sudo mount -t debugfs none /sys/kernel/debug

# Navigate to the secure vault directory
cd /sys/kernel/debug/secure_vault
Getting Process Information
Before creating files, you can check your current process information:
bashcat process_info
# Output will look like:
# PID: 1234
# Session: 456
# PGRP: 789
# UID: 1000
# GID: 1000
Creating a Secure File
You can create a new file with three types of access restrictions:

PID-based restriction
Group-based restriction
Session-based restriction

bash# Create a file restricted to current PID
echo "secret_file pid 1234" > create

# Create a file restricted to a specific group
echo "group_file gid 1000" > create

# Create a file restricted to current session
echo "session_file session 456" > create
Listing Files
To view all files in the vault:
bashcat list
# Output will look like:
# secret_file pid 1234 0
# group_file gid 1000 0
# session_file session 456 0
Writing to Files
To write data to a secure file:
bashecho "This is a secret message only readable by PID 1234" > secret_file
If your current process doesn't match the access control criteria, you'll get a "Permission denied" error.
Reading from Files
To read data from a secure file:
bashcat secret_file
# Output (if access allowed):
# This is a secret message only readable by PID 1234
Deleting Files
To delete a file from the vault:
bashecho "secret_file" > delete
Example: Using with Different Processes
This example demonstrates access control between processes:
bash# In terminal 1:
cd /sys/kernel/debug/secure_vault
cat process_info  # Note your PID, e.g., 1234
echo "my_secure_data pid 1234" > create
echo "This is secret data for PID 1234 only" > my_secure_data

# In terminal 2:
cd /sys/kernel/debug/secure_vault
cat my_secure_data  # Should result in "Permission denied"

# In terminal 1:
cat my_secure_data  # Should work and show the message
Example: Using Session-Based Access
Session-based access allows all processes in the same login session to access the file:
bash# Get your session ID
cat process_info  # Note your session ID, e.g., 456

# Create a session-restricted file
echo "session_data session 456" > create
echo "This is data for session 456" > session_data

# Any process in the same session can access it
cat session_data  # Should work from any process in your session
Example: Using Group-Based Access
Group-based access allows all processes running under a specific group to access the file:
bash# Get your group ID
cat process_info  # Note your GID, e.g., 1000

# Create a group-restricted file
echo "group_data gid 1000" > create
echo "This is data for group 1000" > group_data

# Any process in the same group can access it
cat group_data  # Should work from any process in your group
Security Considerations

The driver uses debugfs, which is typically accessible only to root users. For production use, consider implementing proper user-space access controls.
Files are stored in memory and will be lost when the module is unloaded or the system is rebooted.
The maximum file size is limited to 4MB by default (configurable in the source code).
There's a limit of 256 files in the vault (also configurable).

Troubleshooting

Permission denied when accessing control files: Make sure you have the necessary permissions to access the debugfs files (usually requires root privileges).
Unable to create files: Check if you've reached the maximum file limit (256).
Module loading fails: Check kernel logs (dmesg) for detailed error messages.

Unloading the Module
When you're done using the secure vault:
bashsudo rmmod secure_vault_driver
This will remove all secure files from memory.
