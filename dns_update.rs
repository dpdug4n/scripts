//untested at this point. Uses a syscall to manually update DNS record cache. Needs admin rights. Could be used to bypass network level domain blocks?

use std::env;
use std::ptr;
use std::ffi::CString;
use windows::Win32::Networking::WinSock::*;
use windows::Win32::System::SystemServices::DNS_TYPE_A;
use windows::Win32::Networking::WinInet::*;
use windows::Win32::Foundation::*;
use windows::Win32::Networking::Dns::*;

/// Manually updates DNS cache via dnsapi.dll
fn update_dns_cache(domain: &str, ip_address: &str) -> Result<(), String> {
    unsafe {
        let domain_wide = CString::new(domain).map_err(|e| e.to_string())?;
        
        let mut dns_record = DNS_RECORD {
            pNext: ptr::null_mut(),
            pName: domain_wide.as_ptr() as *mut _,
            wType: DNS_TYPE_A,
            wDataLength: std::mem::size_of::<IN_ADDR>() as u16,
            Flags: DNS_RECORD_FLAGS { Data: DNS_RECORD_FLAGS_0 { Section: DnsSectionAnswer as u32 } },
            Data: DNS_RECORD_0 {
                A: DNS_A_DATA {
                    IpAddress: inet_addr(CString::new(ip_address).unwrap().as_c_str().as_ptr() as *const i8),
                },
            },
        };

        let status = DnsReplaceRecordSetW(&mut dns_record, 0, HANDLE(0), ptr::null_mut(), ptr::null_mut());

        if status.0 == 0 {
            Ok(())
        } else {
            Err(format!("Failed to update DNS cache: Error Code {}", status.0))
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        println!("Usage: dns_update.exe <domain> <ip>");
        return;
    }

    let domain = &args[1];
    let ip = &args[2];

    match update_dns_cache(domain, ip) {
        Ok(_) => println!("Successfully updated DNS cache for {} -> {}", domain, ip),
        Err(e) => eprintln!("Error: {}", e),
    }
}
