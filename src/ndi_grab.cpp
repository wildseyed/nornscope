#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <Processing.NDI.Lib.h>

int main() {
    if (!NDIlib_initialize()) {
        printf("NDIlib_initialize failed\n");
        return 1;
    }

    NDIlib_find_create_t find_desc;
    find_desc.show_local_sources = true;
    find_desc.p_groups = nullptr;
    find_desc.p_extra_ips = nullptr;
    NDIlib_find_instance_t finder = NDIlib_find_create_v2(&find_desc);
    if (!finder) { printf("find_create failed\n"); return 1; }

    printf("Waiting 6s for NDI discovery...\n");
    NDIlib_find_wait_for_sources(finder, 6000);

    uint32_t n = 0;
    const NDIlib_source_t* sources = NDIlib_find_get_current_sources(finder, &n);
    printf("Found %u source(s):\n", n);
    for (uint32_t i = 0; i < n; i++)
        printf("  [%u] name='%s' url='%s'\n", i, sources[i].p_ndi_name,
               sources[i].p_url_address ? sources[i].p_url_address : "(none)");

    if (n == 0) { printf("NO SOURCES FOUND\n"); return 2; }

    NDIlib_recv_create_v3_t recv_desc;
    recv_desc.source_to_connect_to = sources[0];
    recv_desc.color_format = NDIlib_recv_color_format_BGRX_BGRA;
    recv_desc.bandwidth = NDIlib_recv_bandwidth_highest;
    recv_desc.allow_video_fields = false;
    recv_desc.p_ndi_recv_name = "ndi-grab-test";
    NDIlib_recv_instance_t recv = NDIlib_recv_create_v3(&recv_desc);
    if (!recv) { printf("recv_create failed\n"); return 1; }
    NDIlib_recv_connect(recv, &sources[0]);

    printf("Trying to capture a video frame (up to 15s)...\n");
    for (int tries = 0; tries < 150; tries++) {
        NDIlib_video_frame_v2_t video;
        NDIlib_audio_frame_v3_t audio;
        NDIlib_metadata_frame_t meta;
        switch (NDIlib_recv_capture_v3(recv, &video, &audio, &meta, 100)) {
            case NDIlib_frame_type_video: {
                printf("VIDEO FRAME: %dx%d, %d bytes, fourcc=%08x, fps=%d/%d\n",
                       video.xres, video.yres,
                       video.yres * video.line_stride_in_bytes,
                       video.FourCC, video.frame_rate_N, video.frame_rate_D);
                FILE* f = fopen("/tmp/norns_ndi.ppm", "wb");
                fprintf(f, "P6\n%d %d\n255\n", video.xres, video.yres);
                for (int y = 0; y < video.yres; y++) {
                    uint8_t* row = video.p_data + y * video.line_stride_in_bytes;
                    for (int x = 0; x < video.xres; x++) {
                        uint8_t b = row[x*4+0], g = row[x*4+1], r = row[x*4+2];
                        fputc(r, f); fputc(g, f); fputc(b, f);
                    }
                }
                fclose(f);
                printf("Saved frame to /tmp/norns_ndi.ppm\n");
                NDIlib_recv_free_video_v2(recv, &video);
                NDIlib_recv_destroy(recv);
                NDIlib_find_destroy(finder);
                NDIlib_destroy();
                return 0;
            }
            case NDIlib_frame_type_audio:
                NDIlib_recv_free_audio_v3(recv, &audio);
                break;
            case NDIlib_frame_type_metadata:
                NDIlib_recv_free_metadata(recv, &meta);
                break;
            default: break;
        }
    }
    printf("No video frame received in 15s\n");
    return 3;
}
