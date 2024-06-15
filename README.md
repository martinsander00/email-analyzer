# Malware Analysis Tool

**Description**: This project includes a malware analysis tool with a simple SMTP server and client for testing purposes. The SMTP server receives emails and files, which are stored in a specified folder, while the client sends emails and files to the server.

**Stage**: Early stages

## How to Run

### Running the SMTP Server

To run the SMTP server, you need to provide two arguments:

1. The path to the `./smtp` server executable.
2. The path to a folder that contains some `.mbox` files where you want to receive the email and file content.

#### Command

\`\`\`sh
./smtp /path/to/mbox/folder
\`\`\`

#### Example

\`\`\`sh
./smtp /home/user/mbox
\`\`\`

With `mbox` containing a file named `bob.mbox`.

### Running the Client

To run the client, you need to provide the path to a file that you want to send to the server. If you do not provide an existing file, the SMTP server will display a warning indicating that your file is empty.

#### Command

\`\`\`sh
./client /path/to/file
\`\`\`

### Example

\`\`\`sh
./client /home/user/malware.txt
\`\`\`

If the file does not exist, the SMTP server will issue a warning.
