import json
import os
import glob

def convert_to_homa(src_file, dst_file, suffix):
    with open(src_file, 'r') as f:
        data = json.load(f)
    
    # 1. outputFile
    if "outputFile" in data:
        data["outputFile"]["resultFile"] = f"output/homa-{suffix}.json"
        
    # 2. default Config RetxMode
    if "defaultConfig" in data and "RoCEv2Socket" in data["defaultConfig"]:
        data["defaultConfig"]["RoCEv2Socket"]["RetxMode"] = "RTO_ONLY"

    # 3. remove prioRateLimits
    if "topologyConfig" in data and "flowControlConfig" in data["topologyConfig"]:
        for fc in data["topologyConfig"]["flowControlConfig"]:
            if "prioRateLimits" in fc:
                del fc["prioRateLimits"]

    # 4. applications
    if "applicationConfig" in data:
        for app in data["applicationConfig"]:
            if "applicationConfig" in app:
                ac = app["applicationConfig"]
                is_sender = ac.get("SendEnabled", False)
                if ac.get("CongestionType") == "ns3::RoCEv2ExpressPass":
                    ac["CongestionType"] = "ns3::RoCEv2Homa"
                    if "TrafficSizeBytes" in ac:
                        ac["TrafficSizeBytes"] = 1000000
                    ac["FlowPriority"] = 1
                
                # if sender, update congestion config
                if is_sender and "congestionConfig" in app:
                    app["congestionConfig"] = {"UnscheduledBytes": 10000}

    with open(dst_file, 'w') as f:
        json.dump(data, f, indent=4)

if __name__ == "__main__":
    directory = "."
    files = glob.glob("cc-expresspass-*.json")
    for src in files:
        if src == "cc-expresspass-1to1.json" or src == "cc-expresspass-2to1.json":
            # Already have cc-homa-1to1.json and cc-homa-2to1.json but we can overwrite or skip
            # Let's skip them since user already configured them
            print(f"Skipping {src}")
            continue
            
        suffix = src.replace("cc-expresspass-", "").replace(".json", "")
        dst = f"cc-homa-{suffix}.json"
        print(f"Converting {src} -> {dst}")
        convert_to_homa(src, dst, suffix)
