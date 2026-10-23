<div align="center">

# clipboard-image-overlay

**English** | [日本語](README.ja.md)

An OBS plugin that shows a copied image on your stream with a single hotkey.<br>
The image is hidden automatically after a few seconds.<br>
Made for just-chatting streams, so you can say "look at this" without saving the file first.

<!-- Image: demo GIF (docs/images/demo.gif). After adding the image, remove the comment markers around the next line -->
<!-- <img src="docs/images/demo.gif" width="720" alt="Showing a copied image with a hotkey and hiding it automatically"> -->

### **[Download the latest version](https://github.com/KMOdan5/clipboard-image-overlay/releases)**

For Windows: `clipboard-image-overlay-(version)-windows-x64.zip`

</div>

> [!NOTE]
> **Beta testing is in progress.** If you find bugs, have requests, or notice anything, please let us know via [Bug reports and requests](#bug-reports-and-requests).

## Features

| 📋 Copy and show instantly | ⏱️ Hidden automatically after a while | 🗂️ Show again from the history<br>(0.2 and later) |
|---|---|---|
| Just copy an image and press the hotkey to show it on your stream. | Images fade out and are hidden after a while, so you never have to worry about forgetting to hide them. | Choose an image from the list of images you have shown to show it again. |

<details open>
<summary>Details</summary>

- Large images are scaled down automatically so they do not go off the screen
- Examples of images you can show
  - Images copied from a browser or chat app
  - Screenshots taken with `Win + Shift + S`
  - Selections copied from paint tools (with transparency)
  - Image files copied in File Explorer (GIF animations are played)
- Press the pause hotkey to keep the image on screen
- You can switch to another image even before the display time is up
- The transition effect (fade, etc.) can be changed with OBS's built-in Show/Hide Transition

</details>

## Roadmap to 1.0

<!-- Image: roadmap illustration (docs/images/roadmap.png), shared with the Japanese README. After adding the image, remove the comment markers around the next line -->
<!-- <p align="center"><img src="docs/images/roadmap.png" width="720" alt="Roadmap to 1.0"></p> -->

| Release | Version | What's included |
|---|---|---|
| October 2026 | 0.1 (beta) | Beta test (closed test with acquaintances): checking the core features on different environments |
| November 2026 | 0.2 (beta) | Public beta test: adding the history dock and an installer |
| Early December 2026 | 1.0 | Official release: reflecting the test results and improving the manual |

> [!TIP]
> Anyone can join from the public beta test in November.

## Requirements

- Windows 10 / 11
- OBS Studio 31.1.1 or later

Files for macOS are also provided, but they have not been tested.

## Installation and usage

Download the latest version from [Releases](https://github.com/KMOdan5/clipboard-image-overlay/releases). This is the only official place to get the plugin. For installation and usage, see the [manual (Japanese)](docs/manual/ja.md). An English manual will be added in version 1.0.

## Terms of Use

- Free to use for both individuals and companies, including monetized streams.
- No credit is required. If you like it, I would be happy if you mention it on your stream or social media.
- Please make sure you have the rights (such as copyright) to the images you show.
- Modifications and redistribution must follow GPL-2.0.

## Privacy Policy

- This plugin does not connect to the internet and never sends images anywhere.
- Displayed images are saved only in the plugin's folder on your computer.
- AI is used as an aid in developing the system, but this plugin has no AI features and never uses images to train AI.

## Bug reports and requests

Please use the [bug report and request form](https://docs.google.com/forms/d/e/1FAIpQLSeOoU9HNzkLZx70GtSAoAbp-y8T491vJsohMwMT1NJjwKI_xA/viewform?usp=pp_url&entry.11414643=Clipboard+Image+Overlay) (in Japanese). If you use GitHub, you can also use [Issues](https://github.com/KMOdan5/clipboard-image-overlay/issues/new/choose). For security issues, follow the instructions in [SECURITY.md](SECURITY.md).

## Code signing policy

We have applied to SignPath Foundation for code signing of the Windows files.

<details>
<summary>Signing policy and team</summary>

Until signing starts, the distributed files are not signed.

- Only files built by GitHub Actions from the source code in this repository are signed.
- Each release is signed only after the author has checked and approved its contents.
- Every source change is merged through a pull request. The author's account is protected with multi-factor authentication, and release tags are protected so that only the author can create them.

**Team roles**

- Author, reviewer and approver: [KMOdan5](https://github.com/KMOdan5)
  - Defines the requirements and design, and reviews every change, including security checks and refactoring.
- Parts of the code are written with the help of AI (Claude). The author reviews AI-written code before it is merged, too.

For privacy, see the [Privacy Policy](#privacy-policy). To report a security issue, see [SECURITY.md](SECURITY.md).

</details>

## License

GPL-2.0
