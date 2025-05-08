#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>

#define MAX_OS_LEN 256

// Check which OS this is running on
void get_os_name(char *os_name, size_t max_len) {
    FILE *fp = popen("uname -a", "r");
    if (fp == NULL) {
        perror("Failed to run uname");
        strncpy(os_name, "UNKNOWN", max_len);
        return;
    }

    if (fgets(os_name, max_len, fp) == NULL) {
        strncpy(os_name, "UNKNOWN", max_len);
    }

    pclose(fp);
}


void check_file_access(const char* filepath) {
    if (access(filepath, R_OK) == 0) {
        printf("Access to %s: Granted\n", filepath);
    } else {
        printf("Access to %s: Denied\n", filepath);
    }
}

void check_admin_privileges() {
    uid_t euid = geteuid(); 
    printf("Effective User ID: %d\n", euid);

    if (euid == 0) {
        printf("Current user has admin (root) privileges.\n");
    } else {
        printf("Current user does NOT have admin (root) privileges.\n");
    }
}

void test_failed_sudo_attempts(int attempts) {
    int failed = 0;
    printf("\n--- Sudo Password brute-force ---\n");
    for (int i = 0; i < attempts; i++) {
        int ret = system("echo 'xxxxxxx' | sudo -S -k true > /dev/null 2>&1");
        if (ret != 0) {
            failed++;
        }
    }

    printf("Total failed sudo attempts: %d out of %d\n", failed, attempts);
}

void qnx_test_failed_login_attempts(int attempts, const char *user) {
    int failed = 0;

    printf("\n--- Qnx Password Brute-Force ---\n");
    for (int i = 0; i < attempts; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd),
            "echo 'invalidpass' | login %s > /dev/null 2>&1", user);

        int ret = system(cmd);
        if (ret != 0) {
            failed++;
        }
    }

    printf("Total attempts: %d\n", attempts);
    printf("Failed attempts: %d\n", failed);
}

void check_openssl_version() {
    printf("\n--- OpenSSL Version ---\n");
    FILE *fp = popen("openssl version", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            printf("%s", line);
        }
        pclose(fp);
    } else {
        printf("OpenSSL not found or failed to run.\n");
    }
}

void check_openssl_ciphers() {
    printf("\n--- OpenSSL Cipher Summary (SHA256 / SHA384 / POLY1305) ---\n");

    FILE *fp = popen("openssl ciphers -v", "r");
    if (!fp) {
        printf("Failed to list OpenSSL ciphers.\n");
        return;
    }

    char line[512];
    int total = 0;
    int sha256_count = 0;
    int sha384_count = 0;
    int poly1305_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        total++;

        if (strstr(line, "SHA256")) sha256_count++;
        if (strstr(line, "SHA384")) sha384_count++;
        if (strstr(line, "POLY1305")) poly1305_count++;
    }

    pclose(fp);

    printf("Total ciphers found         : %d\n", total);
    printf("Ciphers using SHA256        : %d\n", sha256_count);
    printf("Ciphers using SHA384        : %d\n", sha384_count);
    printf("Ciphers using POLY1305      : %d\n", poly1305_count);
}
void print_shadow_root_line() {
    printf("\n--- Verify root password is encrypted/hashed /etc/shadow ---\n");

    FILE *fp = fopen("/etc/shadow", "r");
    if (!fp) {
        perror("Unable to open /etc/shadow (are you root?)");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "root:", 5) == 0) {
            printf("root entry: %s", line);
            break;
        }
    }

    fclose(fp);
}


void test_shadow_access_as_user1() {
    printf("\n--- Access Control Check (Not Root) using /shadow ---\n");

    int ret = system("su - user1 -c '"
        "echo Current user:; whoami; "
        "echo Checking shadow permissions:; ls -l /etc/shadow; "
        "echo Attempt to read /etc/shadow:; cat /etc/shadow; "
        "echo Attempt to write to /etc/shadow:; echo test >> /etc/shadow; "
        "echo Attempt to remove /etc/shadow:; rm -f /etc/shadow;"
        "'");  

}
void test_shadow_access_as_current_user() {
    printf("\n--- Access Control Check (Non-root) on Ubuntu using /etc/shadow ---\n");

    if (geteuid() == 0) {
        printf("Skipping Access Control tests on /etc/shadow to prevent damage: User is Root.\n");
        return;
    }

    int ret = system(
        "echo Current user:; whoami; "
        "echo Checking shadow permissions:; ls -l /etc/shadow; "
        "echo Attempt to read /etc/shadow:; cat /etc/shadow; "
        "echo Attempt to write to /etc/shadow:; echo test >> /etc/shadow; "
        "echo Attempt to remove /etc/shadow:; rm -f /etc/shadow"
    );


}

void ubuntu_check_tcp_ports() {
    printf("\n--- TCP Listening Ports ---\n");

    FILE *fp = popen("ss -tuln | grep LISTEN", "r");
    if (!fp) {
        perror("Failed to run ss");
        return;
    }

    char line[512];
    int seen_ports[65536] = {0};  // Port range 0-65535

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Local")) continue;

        char *token = strtok(line, " ");
        while (token) {
            if (strchr(token, ':')) {
                char *port_str = strrchr(token, ':');
                if (port_str && *(port_str + 1) != '\0') {
                    int port = atoi(port_str + 1);
                    if (port > 0 && port < 65536 && !seen_ports[port]) {
                        seen_ports[port] = 1;
                        printf("Open TCP port detected: %d\n", port);
                    }
                }
                break;
            }
            token = strtok(NULL, " ");
        }
    }

    pclose(fp);
}
void ubuntu_check_udp_ports() {
    printf("\n--- UDP Listening Ports ---\n");

    FILE *fp = popen("ss -uln", "r");
    if (!fp) {
        perror("Failed to run ss");
        return;
    }

    char line[512];
    int seen_ports[65536] = {0};

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Local")) continue;

        char *token = strtok(line, " ");
        while (token) {
            if (strchr(token, ':')) {
                char *port_str = strrchr(token, ':');
                if (port_str && *(port_str + 1) != '\0') {
                    int port = atoi(port_str + 1);
                    if (port > 0 && port < 65536 && !seen_ports[port]) {
                        seen_ports[port] = 1;
                        printf("Open UDP port detected: %d\n", port);
                    }
                }
                break;
            }
            token = strtok(NULL, " ");
        }
    }

    pclose(fp);
}


void qnx_check_tcp_ports() {
    printf("\n--- TCP Listening Ports ---\n");

    FILE *fp = popen("netstat -an | grep LISTEN", "r");
    if (!fp) {
        perror("Failed to run netstat");
        return;
    }

    char line[512];
    int seen_ports[65536] = {0};

    while (fgets(line, sizeof(line), fp)) {
        // Extract the local address (usually field 4)
        char *token = strtok(line, " ");
        int field = 0;
        while (token != NULL) {
            field++;
            if (field == 4) {
                // Extract port from the IP.port format (e.g., 0.0.0.0.22)
                char *port_str = strrchr(token, '.');
                if (port_str && *(port_str + 1) != '\0') {
                    int port = atoi(port_str + 1);
                    if (port > 0 && port < 65536 && !seen_ports[port]) {
                        seen_ports[port] = 1;
                        printf("Open TCP port detected: %d\n", port);
                    }
                }
                break;
            }
            token = strtok(NULL, " ");
        }
    }

    pclose(fp);
}
void qnx_check_udp_ports() {
    printf("\n--- UDP Listening Ports ---\n");

    FILE *fp = popen("netstat -an | grep udp", "r");
    if (!fp) {
        perror("Failed to run netstat");
        return;
    }

    char line[512];
    int seen_ports[65536] = {0};

    while (fgets(line, sizeof(line), fp)) {
        // Extract the local address (typically in field 4)
        char *token = strtok(line, " ");
        int field = 0;
        while (token != NULL) {
            field++;
            if (field == 4) {
                char *port_str = strrchr(token, '.');
                if (port_str && *(port_str + 1) != '\0') {
                    int port = atoi(port_str + 1);
                    if (port > 0 && port < 65536 && !seen_ports[port]) {
                        seen_ports[port] = 1;
                        printf("Open UDP port detected: %d\n", port);
                    }
                }
                break;
            }
            token = strtok(NULL, " ");
        }
    }

    pclose(fp);
}

void test_network_download_throughput() {
    printf("\n=== Network Download Throughput Test ===\n");

    const char *http_cmd = "curl -s -w \"%{time_total} %{size_download} %{speed_download}\\n\" -o /dev/null http://speedtest.tele2.net/100MB.zip";
    const char *https_cmd = "curl -k -s -w \"%{time_total} %{size_download} %{speed_download}\\n\" -o /dev/null https://proof.ovh.net/files/100Mb.dat";

    double total_http_time = 0.0, total_https_time = 0.0;
    double total_http_speed = 0.0, total_https_speed = 0.0;

    printf("\n[HTTP] Testing download from http://speedtest.tele2.net...\n");
    for (int i = 0; i < 10; i++) {
        FILE *fp = popen(http_cmd, "r");
        if (!fp) continue;

        char buffer[128];
        if (fgets(buffer, sizeof(buffer), fp)) {
            double time = 0.0, size = 0.0, speed = 0.0;
            sscanf(buffer, "%lf %lf %lf", &time, &size, &speed);
            printf("Run %d: %.2f MB in %.3f sec (%.2f MB/s)\n", i + 1, size / (1024 * 1024), time, speed / (1024 * 1024));
            total_http_time += time;
            total_http_speed += speed;
        }
        pclose(fp);
    }

    printf("\n[HTTPS] Testing download from https://proof.ovh.net...\n");
    for (int i = 0; i < 10; i++) {
        FILE *fp = popen(https_cmd, "r");
        if (!fp) continue;

        char buffer[128];
        if (fgets(buffer, sizeof(buffer), fp)) {
            double time = 0.0, size = 0.0, speed = 0.0;
            sscanf(buffer, "%lf %lf %lf", &time, &size, &speed);
            printf("Run %d: %.2f MB in %.3f sec (%.2f MB/s)\n", i + 1, size / (1024 * 1024), time, speed / (1024 * 1024));
            total_https_time += time;
            total_https_speed += speed;
        }
        pclose(fp);
    }

    printf("\n--- Average Download Performance ---\n");
    printf("HTTP  (avg): %.3f sec, %.2f MB/s\n", total_http_time / 10.0, (total_http_speed / 10.0) / (1024 * 1024));
    printf("HTTPS (avg): %.3f sec, %.2f MB/s\n", total_https_time / 10.0, (total_https_speed / 10.0) / (1024 * 1024));
}
void test_https_upload_throughput() {
    printf("\n=== HTTPS Upload Throughput Test ===\n");

    const char *filename = "testfile.dat";

    // Create test file if it doesn't exist
    if (access(filename, F_OK) != 0) {
        printf("Creating 10MB test file: %s\n", filename);
        if (system("dd if=/dev/zero of=testfile.dat bs=1M count=10 status=none") != 0) {
            perror("Failed to create test file");
            return;
        }
    }

    // Get full path to the file
    char full_path[PATH_MAX];
    if (!realpath(filename, full_path)) {
        perror("Failed to resolve full path to test file");
        return;
    }

    double total_time = 0.0;

    for (int i = 0; i < 10; i++) {
        char cmd[PATH_MAX + 200];
        snprintf(cmd, sizeof(cmd),
            "curl -s -F file=@%s https://0x0.st -w \"%%{time_total}\\n\" -o /dev/null",
            full_path);

        FILE *fp = popen(cmd, "r");
        if (!fp) {
            perror("Failed to run curl");
            continue;
        }

        char buffer[128];
        if (fgets(buffer, sizeof(buffer), fp)) {
            double time = atof(buffer);
            printf("Run %2d: Upload time = %.3f seconds\n", i + 1, time);
            total_time += time;
        }

        pclose(fp);
    }

    // OS-specific cleanup
    char os_name[MAX_OS_LEN] = {0};
    get_os_name(os_name, sizeof(os_name));

    if (strstr(os_name, "Ubuntu")) {
        remove(filename);
    } else if (strstr(os_name, "QNX")) {
        system("find / -name testfile.dat -exec rm -f {} \\;");
    }

    double avg_time = total_time / 20.0;
    double avg_bitrate = (10.0 * 8.0) / avg_time; // 10MB * 8 = 80Mb

    printf("\n--- Average Upload Performance ---\n");
    printf("Avg Time: %.3f sec\n", avg_time);
    printf("Avg Bitrate: %.2f Mbps\n", avg_bitrate);
}



void check_auth_log() {
    const char *log_path = "/var/log/auth.log";
    printf("\n--- Authentication Log Check ---\n");

    if (access(log_path, R_OK) == 0) {
        printf("%s exists. Showing first 2 lines:\n\n", log_path);
        FILE *fp = popen("head -n 2 /var/log/auth.log", "r");
        if (!fp) {
            perror("Failed to read auth.log");
            return;
        }

        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            printf("%s", line);
        }

        pclose(fp);
    } else {
        printf("%s not found or access denied.\n", log_path);
    }

    printf("\n--- Pluggable Authentication Module(PAM) Check ---\n");
    int pam_found = 0;

    if (access("/etc/pam.d", F_OK) == 0 || access("/etc/pam.conf", F_OK) == 0) {
        pam_found = 1;
    } else {
        FILE *fp = popen("find /lib /usr/lib -name 'libpam.so*' 2>/dev/null", "r");
        if (fp) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), fp)) {
                pam_found = 1;
            }
            pclose(fp);
        }
    }

    if (pam_found) {
        printf("PAM is present on this OS.\n");
    } else {
        printf("PAM not found on this OS.\n");
    }
}



int main() {
	
	// Check current operating system.
    char os_name[MAX_OS_LEN] = {0};
    get_os_name(os_name, sizeof(os_name));
    printf("Detected OS: %s\n", os_name);
	
	// Authentication Tests
    check_admin_privileges();
    check_file_access("/etc/passwd");
    check_file_access("/etc/shadow");
	check_auth_log();


    if (strstr(os_name, "Ubuntu")) { // This can probably just be Linux
        test_failed_sudo_attempts(10); // Authentication test
        test_shadow_access_as_current_user(); // Access Control Test
		ubuntu_check_tcp_ports(); // Distributed - TCP Port checker
		ubuntu_check_udp_ports(); // Distributed - UDP Port checker
    } else if (strstr(os_name, "QNX")) { // This could probably include macos / bsd 
        qnx_test_failed_login_attempts(5, "user1"); // 
        test_shadow_access_as_user1();  // Access Control Test
		qnx_check_tcp_ports(); // Distributed - TCP Port Checker
		qnx_check_udp_ports(); // Distributed - UDP Port Checker

    } else {
        printf("Unsupported or unrecognized OS. No OS-specific tests run.\n");
    }
	
	// Cryptography Tests
	check_openssl_version();
    check_openssl_ciphers();
	print_shadow_root_line();
	
	// Throughput Tests
	test_network_download_throughput();
	test_https_upload_throughput();
	
    return 0;
}