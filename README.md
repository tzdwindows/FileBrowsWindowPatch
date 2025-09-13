# FileBrowseWindowPatch

一个使用 C++20 开发的 Windows 平台工具，用于实现 Windows 文件资源管理器窗口的背景模糊效果。
A Windows platform tool developed using C++20, used to achieve the background blur effect of Windows File Explorer window.

## 快速开始 Quick Start
1. 在 [releases](https://github.com/tzdwindows/FileBrowsWindowPatch/releases) 中找到最新版下载
2. 将下载到的 *FileBrowsWindowPatchXXXX.zip* 解压到你认为安全的文件夹中
3. 直接点击 *执行透明.bat* 运行

## 运行效果 Running Effect
运行效果如下：
![运行效果](https://github.com/tzdwindows/FileBrowsWindowPatch/blob/master/Test1.png)

## 使用指南：FileBrowsWindow 效果设置

### 快速开始
1. 按下 `Win + R` 打开运行窗口
2. 输入 **cmd** 并回车，启动命令提示符
3. 使用以下命令进入程序目录：
   ```cmd
   cd <FileBrowsWindowPatch的解压路径>
   ```
4. 运行程序并添加所需参数：
   ```cmd
   FileBrowsWindow.exe [参数选项]
   ```

### 参数说明
程序支持以下参数选项：

| 参数 | 说明 | 示例 |
|------|------|------|
| `--efftype` | 设置模糊类型（取值范围：0-4） | `--efftype=1` |
| `--blendcolor` | 设置模糊颜色，使用[r,g,b,a]格式（0-255） | `--blendcolor=[255,255,255,255]` |

### 使用示例
- 单独使用效果类型参数：
  ```cmd
  FileBrowsWindow.exe --efftype=2
  ```

- 单独使用颜色参数：
  ```cmd
  FileBrowsWindow.exe --blendcolor=[255,0,0,128]
  ```

- 组合使用多个参数（参数可叠加）：
  ```cmd
  FileBrowsWindow.exe --efftype=3 --blendcolor=[0,128,255,200]
  ```

### 注意事项
- 参数顺序无关紧要，程序会自动识别
- 颜色值需确保在0-255范围内
- 效果类型请选择0-4之间的整数

提示：将 `<FileBrowsWindowPatch的解压路径>` 替换为实际的解压目录路径