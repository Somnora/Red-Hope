from huggingface_hub import snapshot_download
# SDXL base
snapshot_download("stabilityai/stable-diffusion-xl-base-1.0",
    allow_patterns=["*.json","*.txt","*fp16*","*.model","vae/*","text_encoder*/*","tokenizer*/*","unet/*fp16*","scheduler/*"])
print("SDXL base done")
# IP-Adapter (SDXL image encoder + adapters)
snapshot_download("h94/IP-Adapter",
    allow_patterns=["sdxl_models/*","models/image_encoder/*"])
print("IP-Adapter done")
