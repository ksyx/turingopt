# Turing HPC Cluster Optimization Project

## Job Monitoring Daemon (Watcher)
This daemon is a complete data pipeline for independent use by both
regular users and administrators of the HPC cluster to collect
resource utilization data of jobs, perform analyses and generate
reports that can be received through email or checked on-demand
through a website using provided web server. This tool is designed
for SLURM and tested using version 21.08. To ease in quick start,
SLURM batch submission template and systemd service file templates
are provided for users to start the daemon with job submission script,
and for administrator to start the dameon in a dedicated node,
respectively. **Check out [slides](./slides-web.pdf) for an
introduction of its purpose, functionality and design!**

### Building
After cloning the repo, please `cd` into repo directory and do
these in preparation of compiling:
- Edit `meson.build` to select GPU measurement source
- Change directory into `watcher` and run
  `cp src/analyze_mail_config.c{.in,}`, then edit
  `src/analyze_mail_config.c` to customize email content and
  recipients.
- Edit these files in `watcher/include` to set running parameters:
  - `common.h`
    - `SLURM_TRACK_STEPS_REMOVED`: follow the comment to specify how
       to be compatible with this change
    - `DEFAULT_DB_PATH`: Default path to install the database
    - `SLURMDB_RECONNECT_*`: reconnection limits
  - `worker.h`
    - `PRODUCTION_FREQ`: whether this is a test run or production run
    - `ANALYZE_PERIOD_LENGTH`: time to wait before changing to new
      period
    - `ACCOUNTING_RPC_INTERVAL`: time between each import from SLRUM
      accounting database
    - `SCRAPE_INTERVAL`: time to wait between each scraping
    - `TOTAL_SCRAPE_TIME_PER_NODE`: length of time of each scraper run
    - `SCRAPE_CONCURRENT_NODES`: number of nodes to request allocation
      at once. Note that it will at most request allocating double of
      this number.
    - `ALLOCATION_TIMEOUT`: time to wait before deciding current
      allocation request as failed.
  - If web server is needed, `webserver/server.go`:
    - `userAuthResultField`: refer to web server configuration

In project root, run `meson setup build` to set up a building
directory. If missing dependency is prompted, please use
`build_*_pkg_config.sh` in the directory to build their package
configs. When using NVML, the path to CUDA toolkit need to be added
to PKG_CONFIG_PATH prior to running the building script, as in
```
export PKG_CONFIG_PATH=/path/to/cuda/toolkit/version/targets/x86_64-linux/lib/pkgconfig:$PKG_CONFIG_PATH
```

After meson have successfully set up the building directory, run
`ninja` to build binaries.

### Usage

The daemon can be used in multiple ways, following template scripts
provided in various directories in `watcher/`:

- By regular users, attaching to job submission: Port your submission
  script onto the template provided at
  `scripts/user_submission/submit.sh` following instructions
  in the file and configure `watch.sh` in the same directory. Submit
  `submit.sh` with sbatch.
- By administrators, but still usable by regular users, to sample the
  cluster with a long SLURM job: modify the script templates in
  `scripts/slurm_scraping_jobs` and run `run.sh` with bash.
- By administrators, requires root privilege and preferably on a
  separate node: create dedicated directory for the tool and copy
  build/watcher/{turingwatch,webserver/turingreport} into the
  directory. Within the directory, run `systemd/gen_env.sh`,
  modify **copies of systemd service file templates** in `systemd/`
  following instructions in the file and copy them into
  `/etc/systemd/system`. Reload service files with running
  `systemctl daemon-reload` and use
  `systemctl start turingwatch.{server,distributor}.service`
  to start watcher server or distributor daemons, respectively.

After the daemon has been running for a period of time, usually 2 or
3 times of `ACCOUNTING_RPC_INTERVAL`, result tars will appear in
`analysis_result` directory, from which one can:

- Visit reporting website if the provided webserver is running
- Run the result postprocessing pipeline script with templates in
  `scripts/scheduled_worker` **in the directory containing
  `analysis_results/`**, or schedule the execution of these scripts
  with SLURM by using `sbatch -b` or with systemd by enabling the
  timer service using
  `systemctl enable --now turingwatch.scheduled_worker.timer`,
  after modifying
  `systemd/turingwatch.scheduled_worker.{timer,service}` and copying
  to `/etc/systemd/system`.
- Directly untar the tarball and manually send email using
  `mail -t <<< $(cat username.mail.header username.mail)`

#### Example Web Server Configuration

Put files in `watcher/webserver/static` in a document root directory
and configure Apache HTTP Server as follows:
```apache
<VirtualHost *:443>
	# ...
	SSLEngine on
	SSLCertificateKeyFile "..."
	SSLCertificateFile "..."

	# Reverse proxy into the application is necessary
	ProxyPass /api http://localhost:8080/api
	ProxyPassReverse /api http://localhost:8080/api

	# Azure Active Directory
	OIDCRedirectURI /auth/callback
	OIDCProviderMetadataURL https://login.microsoftonline.com/tenantid/v2.0/.well-known/openid-configuration
	OIDCScope "openid profile"
        OIDCProviderAuthRequestMethod POST
        OIDCClientID ...
	OIDCClientSecret ...
	OIDCCookie auth_session
        OIDCStateMaxNumberOfCookies 7 true
        OIDCRemoteUserClaim upn
        OIDCPassClaimsAs both
        OIDCAuthNHeader X-AuthUser
	OIDCSessionInactivityTimeout 28700 # 8 hours
	<Location />
                SSLRequireSSL
                SSLOptions +StdEnvVars
                AllowOverride AuthConfig Limit
                AuthType openid-connect
                Require valid-user
                Order allow,deny
                Allow from all
        </Location>
</VirtualHost>
```

After configuring systemd service scripts as previously mentioned,
run `systemctl start turingwatch.webserver.service`.

## Optimization layer (Intercepter)
The dynamic libraries are built along with watcher in `intercepter/`
and are intended to be used together with `LD_PRELOAD`. Currently a
malloc/free replacement that holds off large chunk of memory from
freeing and reuse them before going through actual glibc
implementation is implemented lock-free, but have not been
throughfully tested yet, due to lack of samples. This layer can
also be extended for more infrastructure-specific optimizations.
