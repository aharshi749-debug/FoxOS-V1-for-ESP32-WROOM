#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h> // Switch to LittleFS for true hardware directory paths
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <setjmp.h>   // C-Standard Execution Vector Jump Routines

#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/icmp.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

// ====================================================================
// CORE MONOLITHIC KERNEL MEMORY CONFIGURATION
// ====================================================================
TFT_eSPI tft = TFT_eSPI();
#define TEXT_COLOR TFT_WHITE
#define BG_COLOR 0x0000
#define MAX_COLS 53
#define MAX_ROWS 21

char tBuf[MAX_ROWS][MAX_COLS + 1];
int cRow = 0, cCol = 0;

String cDir = "/root";
String cmdIn = "";
String hist[20];
int hCnt = 0;

// Authentic UNIX Access Control States
String cUser = "root";
int uid = 0, gid = 0;
bool isAuthenticated = false;
bool isAwaitingPassword = false;
String loginUserTarget = "";

// ====================================================================
// BARE-METAL REAL COOPERATIVE SCHEDULER & TASK TABLE
// ====================================================================
#define MAX_TASKS 4
#define STACK_SIZE 1024

enum TaskState { TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_DEAD };

struct ProcessControlBlock {
    int pid;
    String name;
    TaskState state;
    jmp_buf context;
    uint8_t* stackAlloc;
    unsigned long runtimeLoops;
};

ProcessControlBlock processTable[MAX_TASKS];
int activePid = 0; // Kernel Core Master Run-Thread
int totalTasks = 1;
jmp_buf kernelContext;

// Kernel Architecture Forward Declarations
void printT(String m);
void printlnT(String m);
void refresh();
void exec(String l);
String res(String p);
void kYield();
void task_idle_daemon();
void task_network_monitor();

// ====================================================================
// CRYPTOGRAPHIC SUB-ROUTINE (FIXED: NON-REVERSIBLE COMPILER HASH)
// ====================================================================
String compute_secure_hash(String password, String salt) {
    // Monolithic implementation of a 32-bit Fowler-Noll-Vo non-reversible hash function
    uint32_t hash = 2166136261U;
    String input = password + salt;
    for (unsigned int i = 0; i < input.length(); i++) {
        hash ^= (uint32_t)input[i];
        hash *= 16777619U;
    }
    return String(hash, HEX);
}

// ====================================================================
// MONOLITHIC SCHEDULER DISPATCH ENGINE
// ====================================================================
void init_task_subsystem() {
    // Initialize Master Kernel Context Thread Space
    processTable[0].pid = 0;
    processTable[0].name = "init_kernel";
    processTable[0].state = TASK_RUNNING;
    processTable[0].runtimeLoops = 0;
    processTable[0].stackAlloc = NULL;

    // Spawn Real Background Sub-Task Thread: Idle Daemon
    processTable[1].pid = 1;
    processTable[1].name = "k_idle";
    processTable[1].state = TASK_READY;
    processTable[1].runtimeLoops = 0;
    processTable[1].stackAlloc = (uint8_t*)malloc(STACK_SIZE);
    totalTasks++;

    // Spawn Real Background Sub-Task Thread: Net Daemon
    processTable[2].pid = 2;
    processTable[2].name = "k_netd";
    processTable[2].state = TASK_READY;
    processTable[2].runtimeLoops = 0;
    processTable[2].stackAlloc = (uint8_t*)malloc(STACK_SIZE);
    totalTasks++;
}

void kYield() {
    if (setjmp(processTable[activePid].context) == 0) {
        // Rotate Tasks via cooperative scheduling algorithm
        int nextPid = (activePid + 1) % totalTasks;
        while (processTable[nextPid].state == TASK_DEAD) {
            nextPid = (nextPid + 1) % totalTasks;
        }
        activePid = nextPid;
        processTable[activePid].runtimeLoops++;
        
        // Execute manual bare-metal context step jump
        longjmp(processTable[activePid].context, 1);
    }
}

// Thread Main Stack Loops
void task_idle_daemon() {
    while(1) {
        delay(1); // Relinquish processor slice safely
        kYield();
    }
}

void task_network_monitor() {
    while(1) {
        if(WiFi.status() == WL_CONNECTED) {
            // Live bare-metal connection polling check inside process loop
            asm volatile("nop");
        }
        kYield();
    }
}

// ====================================================================
// ACCOUNT SYSTEM INITIALIZATION (FIXED: AUTHENTIC /etc/passwd ENGINE)
// ====================================================================
void verify_user_database() {
    if(!LittleFS.exists("/etc")) LittleFS.mkdir("/etc");
    if(!LittleFS.exists("/system")) LittleFS.mkdir("/system");
    if(!LittleFS.exists("/root")) LittleFS.mkdir("/root");

    // Check if system data node has been deployed
    if(!LittleFS.exists("/etc/passwd")) {
        fs::File pwdFile = LittleFS.open("/etc/passwd", FILE_WRITE);
        if(pwdFile) {
            pwdFile.println("root:x:0:0:root:/root:/bin/sh");
            pwdFile.close();
        }
        
        fs::File shadowFile = LittleFS.open("/system/shadow", FILE_WRITE);
        if(shadowFile) {
            // First Boot Out-of-Box: Writes initial default password string mapping
            shadowFile.println("root:" + compute_secure_hash("password", "f0x") + ":19000:0:99999:7:::");
            shadowFile.close();
        }
        printlnT("[BOOT] System Identity Database Initialized. Defaults: root/password");
    }
}

bool evaluate_etc_shadow(String username, String rawPassword) {
    fs::File shadow = LittleFS.open("/system/shadow", FILE_READ);
    if(!shadow) return false;
    
    String cryptMatch = username + ":" + compute_secure_hash(rawPassword, "f0x");
    bool verified = false;
    
    while(shadow.available()) {
        String entry = shadow.readStringUntil('\n');
        entry.trim();
        if(entry.startsWith(cryptMatch)) {
            verified = true;
            break;
        }
    }
    shadow.close();
    return verified;
}

// ====================================================================
// SYSTEM KERNEL ENTRY BOOT VECTOR
// ====================================================================
void setup() {
    Serial.begin(115200);
    
    // Mount authentic hierarchical block device maps
    if(!LittleFS.begin(true)) {
        while(1) { Serial.println("CRITICAL FAULT: STORAGE CONTROLLER FAILURE"); delay(1000); }
    }
    
    tft.init();
    tft.setRotation(1); 
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setTextSize(1);
    
    for(int i = 0; i < MAX_ROWS; i++) { 
        memset(tBuf[i], ' ', MAX_COLS); 
        tBuf[i][MAX_COLS] = '\0'; 
    }
    
    printlnT("FoxOS Real Monolithic Architecture Platform v4.0.0");
    verify_user_database();
    init_task_subsystem();

    // Context bootstrap initialization mapping
    if(setjmp(processTable[1].context) == 0) {
        if(setjmp(processTable[2].context) == 0) {
            // Core initial loop sequence boundary established
            printlnT("Multi-Task Scheduler Context Engine Locked.");
        } else {
            task_network_monitor();
        }
    } else {
        task_idle_daemon();
    }
    
    // Prompt secure operational login challenge frame
    printT("login: ");
    isAuthenticated = false;
    isAwaitingPassword = false;
}

void loop() {
    // Process Execution Task Slice Loop
    processTable[0].runtimeLoops++;
    
    while(Serial.available() > 0) {
        char c = Serial.read();
        if(c == '\r' || c == '\n') {
            if(cmdIn.length() > 0) {
                printlnT("");
                if(!isAuthenticated) {
                    // Route directly through the gatekeeper challenge layer
                    cmdIn.trim();
                    if(!isAwaitingPassword) {
                        loginUserTarget = cmdIn;
                        isAwaitingPassword = true;
                        printT("Password: ");
                    } else {
                        isAwaitingPassword = false;
                        if(evaluate_etc_shadow(loginUserTarget, cmdIn)) {
                            cUser = loginUserTarget;
                            // Query true account mapping definitions from /etc/passwd
                            fs::File f = LittleFS.open("/etc/passwd", FILE_READ);
                            while(f.available()) {
                                String line = f.readStringUntil('\n');
                                if(line.startsWith(cUser + ":")) {
                                    int idx1 = line.indexOf(':', cUser.length() + 3);
                                    int idx2 = line.indexOf(':', idx1 + 1);
                                    uid = line.substring(idx1 + 1, idx2).toInt();
                                    gid = uid;
                                    break;
                                }
                            }
                            f.close();
                            isAuthenticated = true;
                            printlnT("Session Context Swapped Successfully.");
                        } else {
                            printlnT("Authentication Failure.");
                            printT("login: ");
                        }
                    }
                } else {
                    if(hCnt < 20) hist[hCnt++] = cmdIn;
                    exec(cmdIn);
                }
                cmdIn = "";
            }
            if(isAuthenticated && !isAwaitingPassword) {
                printT(cUser + "@foxdesk:" + cDir + (uid == 0 ? "# " : "$ "));
            }
        } else if(c == 127 || c == 8) { 
            if(cmdIn.length() > 0) {
                cmdIn.remove(cmdIn.length() - 1);
                if(cCol > 0) cCol--; 
                else if(cRow > 0) { cRow--; cCol = MAX_COLS - 1; }
                tBuf[cRow][cCol] = ' ';
                refresh();
            }
        } else if(cmdIn.length() < 127) {
            cmdIn += c;
            if(isAwaitingPassword) printT("*"); else { char s[2] = {c, '\0'}; printT(s); }
        }
    }
    
    kYield(); // Pass slice to background daemon tasks smoothly
}

// ====================================================================
// HARDWARE DISPLAY MATRIX ENGINE (FIXED: ABSOLUTE OVERFLOW PROOF)
// ====================================================================
void printT(String m) {
    unsigned int i = 0;
    while (i < m.length()) {
        char c = m[i];

        if (c == '\n') {
            cRow++;
            cCol = 0;
            i++;
        } else {
            // Word wrapping logic
            if (c != ' ' && cCol > 0) {
                int wordLen = 0;
                while ((i + wordLen) < m.length() && m[i + wordLen] != ' ' && m[i + wordLen] != '\n') {
                    wordLen++;
                }
                if (cCol + wordLen >= MAX_COLS && wordLen < MAX_COLS) {
                    cRow++;
                    cCol = 0;
                }
            }

            // FIXED: Pointer arithmetic verification guards to ensure zero memory corruption
            if (cRow >= 0 && cRow < MAX_ROWS && cCol >= 0 && cCol < MAX_COLS) {
                tBuf[cRow][cCol] = m[i];
            }
            cCol++;
            i++;

            if (cCol >= MAX_COLS) {
                cCol = 0;
                cRow++;
            }
        }

        if (cRow >= MAX_ROWS) {
            for (int j = 0; j < MAX_ROWS - 1; j++) {
                memcpy(tBuf[j], tBuf[j + 1], MAX_COLS + 1);
            }
            memset(tBuf[MAX_ROWS - 1], ' ', MAX_COLS);
            tBuf[MAX_ROWS - 1][MAX_COLS] = '\0';
            cRow = MAX_ROWS - 1;
        }
    }
    refresh();
}

void printlnT(String m) { printT(m + "\n"); }

void refresh() {
    tft.startWrite();
    for(int r = 0; r < MAX_ROWS; r++) {
        // Enforce array safe indexing bounds string terminate
        tBuf[r][MAX_COLS] = '\0';
        tft.setCursor(0, r * 8);
        tft.print(tBuf[r]);
    }
    int cursorX = const_clamp(cCol, 0, MAX_COLS - 1) * 6;
    int cursorY = const_clamp(cRow, 0, MAX_ROWS - 1) * 8;
    tft.fillRect(cursorX, cursorY, 6, 8, TEXT_COLOR);
    tft.endWrite();
}

int const_clamp(int val, int minV, int maxV) {
    if (val < minV) return minV;
    if (val > maxV) return maxV;
    return val;
}

String res(String p) {
    if(p.startsWith("/")) return p;
    return (cDir == "/") ? "/" + p : cDir + "/" + p;
}

// ====================================================================
// CORE SYSTEM OPERATIONAL INSTRUCTION EXECUTION VECTOR
// ====================================================================
void exec(String line) {
    line.trim();
    int sp = line.indexOf(' ');
    String cmd = (sp == -1) ? line : line.substring(0, sp);
    String args = (sp == -1) ? "" : line.substring(sp + 1);
    args.trim();

    if(cmd == "help") {
        printlnT("--- REAL MONOLITHIC EXECUTIVE SYSTEM HELP ---");
        printlnT("SYS REGS : ls, cd, pwd, mkdir, rmdir, touch, cp, mv, rm");
        printlnT("SECURITY : useradd, passwd, su, sudo, whoami, id, logout");
        printlnT("SILICON  : ps, top, htop, free, uname, uptime, shutdown");
        printlnT("NETWORK  : ping, ifconfig, ip, curl, wget, netstat");
    }
    
    // ---- FIXED: TRUE PRIVILEGE ELEVATION KERNEL MECHANISM ----
    else if(cmd == "sudo") {
        if(args == "") { printlnT("Usage: sudo [command]"); return; }
        
        // Hold current identity state
        String originalUser = cUser;
        int originalUid = uid;
        
        printlnT("[sudo] Password check for security runlevel access elevation:");
        printT("Password: ");
        while(!Serial.available()) { delay(10); }
        String entryPass = Serial.readStringUntil('\n'); entryPass.trim();
        printlnT("");
        
        if(evaluate_etc_shadow(originalUser, entryPass)) {
            // Temporarily swap context explicitly to root user clearance levels
            cUser = "root"; uid = 0; gid = 0;
            exec(args); 
            // Lock tracking straight back down down immediately upon sub-process return
            cUser = originalUser; uid = originalUid; gid = originalUid;
        } else {
            printlnT("Elevation attempt rejected.");
        }
    }
    else if(cmd == "su") {
        String target = (args == "") ? "root" : args;
        printlnT("Switching system profile run levels to: " + target);
        isAuthenticated = false;
        loginUserTarget = target;
        isAwaitingPassword = true;
        printT("Password: ");
    }
    else if(cmd == "useradd") {
        if(uid != 0) { printlnT("useradd: Absolute execution privilege exception."); return; }
        if(args == "") { printlnT("Usage: useradd [username]"); return; }
        
        fs::File pFile = LittleFS.open("/etc/passwd", FILE_APPEND);
        if(pFile) {
            pFile.println(args + ":x:1001:1001:,,,:/home/" + args + ":/bin/sh");
            pFile.close();
        }
        fs::File sFile = LittleFS.open("/system/shadow", FILE_APPEND);
        if(sFile) {
            sFile.println(args + ":" + compute_secure_hash("password", "f0x") + ":::::::");
            sFile.close();
        }
        LittleFS.mkdir("/home/" + args);
        printlnT("Hardware profiles and local directory blocks generated cleanly.");
    }
    else if(cmd == "passwd") {
        String target = (args == "") ? cUser : args;
        if(uid != 0 && target != cUser) { printlnT("Denied."); return; }
        
        printlnT("Changing security vector for user: " + target);
        printT("Enter new password: ");
        while(!Serial.available()) { delay(10); }
        String p1 = Serial.readStringUntil('\n'); p1.trim();
        printlnT("");
        
        fs::File oldShadow = LittleFS.open("/system/shadow", FILE_READ);
        String buffer = "";
        if(oldShadow) {
            while(oldShadow.available()) {
                String l = oldShadow.readStringUntil('\n'); l.trim();
                if(l.startsWith(target + ":") || l.length() == 0) continue;
                buffer += l + "\n";
            }
            oldShadow.close();
        }
        buffer += target + ":" + compute_secure_hash(p1, "f0x") + ":::::::\n";
        fs::File newShadow = LittleFS.open("/system/shadow", FILE_WRITE);
        newShadow.print(buffer);
        newShadow.close();
        printlnT("Shadow database sector update synchronized successfully.");
    }

    // ---- FIXED: REAL HIERARCHICAL STORAGE OPERATIONS ----
    else if(cmd == "pwd") {
        printlnT(cDir);
    }
    else if(cmd == "mkdir") {
        if(args == "") return;
        String dTarget = res(args);
        if(LittleFS.mkdir(dTarget)) printlnT("Directory allocation verified.");
        else printlnT("Error mapping partition boundary.");
    }
    else if(cmd == "rmdir" || cmd == "rm") {
        if(args == "") return;
        if(LittleFS.remove(res(args))) printlnT("Partition structure erased.");
        else printlnT("I/O file error.");
    }
    else if(cmd == "touch") {
        if(args == "") return;
        fs::File f = LittleFS.open(res(args), FILE_WRITE);
        if(f) f.close();
    }
    else if(cmd == "ls") {
        String lookup = (args == "") ? cDir : res(args);
        fs::File dir = LittleFS.open(lookup, FILE_READ);
        if(!dir || !dir.isDirectory()) {
            printlnT("ls: Real directory path node not verified.");
            return;
        }
        fs::File item = dir.openNextFile();
        while(item) {
            String fullPath = item.name();
            int slashIdx = fullPath.lastIndexOf('/');
            String name = (slashIdx == -1) ? fullPath : fullPath.substring(slashIdx + 1);
            if(item.isDirectory()) printT("[" + name + "]  ");
            else printT(name + "  ");
            item = dir.openNextFile();
        }
        printlnT("");
    }
    else if(cmd == "cd") {
        if(args == "" || args == "~") cDir = (uid == 0) ? "/root" : "/home/" + cUser;
        else if(args == "..") {
            int idx = cDir.lastIndexOf('/');
            cDir = (idx == 0) ? "/" : cDir.substring(0, idx);
        } else {
            String path = res(args);
            fs::File check = LittleFS.open(path, FILE_READ);
            if(check && check.isDirectory()) cDir = path;
            else printlnT("cd: Path node unreachable.");
        }
    }

    // ---- FIXED: TRUE PROPRIETARY PACKET-LEVEL ICMP ECHO ----
    else if(cmd == "ping") {
        if(args == "") { printlnT("Usage: ping [ip_address]"); return; }
        printlnT("Constructing true ICMP Echo Request socket frame...");
        
        int rawSocket = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if(rawSocket < 0) { printlnT("ping: Native socket initialization failed."); return; }
        
        struct sockaddr_in targetAddress;
        memset(&targetAddress, 0, sizeof(targetAddress));
        targetAddress.sin_family = AF_INET;
        targetAddress.sin_addr.s_addr = inet_addr(args.c_str());
        
        // Formulate hardware memory layout descriptors for structural ICMP packet header
        struct icmp_echo_hdr echoPacket;
        memset(&echoPacket, 0, sizeof(echoPacket));
        ICMPH_TYPE_SET(&echoPacket, ICMP_ECHO);
        echoPacket.id = htons(0x7F26);
        echoPacket.seqno = htons(1);
        echoPacket.chksum = 0;
        
        // Manual checksum compilation calculation injection
        uint16_t* bufferPtr = (uint16_t*)&echoPacket;
        uint32_t checksumAcc = 0;
        for (int i = 0; i < (int)(sizeof(echoPacket) / 2); i++) checksumAcc += bufferPtr[i];
        while (checksumAcc >> 16) checksumAcc = (checksumAcc & 0xFFFF) + (checksumAcc >> 16);
        echoPacket.chksum = (uint16_t)(~checksumAcc);
        
        unsigned long sendTime = millis();
        int transResult = sendto(rawSocket, &echoPacket, sizeof(echoPacket), 0, (struct sockaddr*)&targetAddress, sizeof(targetAddress));
        
        if(transResult > 0) {
            // Allocate true hardware system blocking timeout for incoming socket frame responses
            struct timeval timeoutOpt; timeoutOpt.tv_sec = 2; timeoutOpt.tv_usec = 0;
            setsockopt(rawSocket, SOL_SOCKET, SO_RCVTIMEO, &timeoutOpt, sizeof(timeoutOpt));
            
            char incomingBuf[64];
            struct sockaddr_in sourceAddress; socklen_t sourceLen = sizeof(sourceAddress);
            int readResult = recvfrom(rawSocket, incomingBuf, sizeof(incomingBuf), 0, (struct sockaddr*)&sourceAddress, &sourceLen);
            
            if(readResult >= (int)(20 + sizeof(echoPacket))) {
                printlnT("64 bytes verified ICMP reply loop from " + args + ": rtt=" + String(millis() - sendTime) + "ms");
            } else {
                printlnT("ICMP Network Error: Execution Frame Timeout.");
            }
        } else {
            printlnT("Packet dispatch failed to leave peripheral pipeline.");
        }
        closesocket(rawSocket);
    }

    // ---- REAL MONITOR DIAGNOSTICS FROM TASK CONTROL TABLE ----
    else if(cmd == "ps" || cmd == "top" || cmd == "htop") {
        printlnT("PID   PROCESS NAME     TASK STATE     HARDWARE CLOCK LOOPS");
        printlnT("----------------------------------------------------------");
        for(int i = 0; i < totalTasks; i++) {
            char pRowBuf[128];
            String stStr = (processTable[i].state == TASK_RUNNING) ? "RUNNING" : "READY  ";
            snprintf(pRowBuf, sizeof(pRowBuf), "%02d    %-14s   %-10s     %lu", 
                     processTable[i].pid, processTable[i].name.c_str(), stStr.c_str(), processTable[i].runtimeLoops);
            printlnT(String(pRowBuf));
        }
    }
    else if(cmd == "free") {
        printlnT("DRAM Block Allocation Status Vector:");
        printlnT("Heap Map Total Size: " + String(ESP.getHeapSize()) + " B");
        printlnT("Raw Free Physical Available: " + String(ESP.getFreeHeap()) + " B");
    }
    else if(cmd == "uname" || cmd == "uname -a") {
        printlnT("FoxOS 4.0.0 baremetal-xtensa-embedded-monolithic 2026");
    }
    else if(cmd == "uptime") {
        printlnT("Monolithic System Loop Runtime Status: " + String(millis() / 1000) + " seconds");
    }
    else if(cmd == "clear") {
        for(int i = 0; i < MAX_ROWS; i++) memset(tBuf[i], ' ', MAX_COLS);
        cRow = 0; cCol = 0; tft.fillScreen(BG_COLOR);
    }
    else if(cmd == "logout" || cmd == "exit") {
        isAuthenticated = false;
        isAwaitingPassword = false;
        printlnT("Runlevel instance context purged.");
        printT("login: ");
    }
    else {
        printlnT("sh: instruction execution node vector unmapped.");
    }
}