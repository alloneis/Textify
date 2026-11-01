#!/usr/bin/env bash
set -e

# 默认配置
DEFAULT_NAME=$(basename "$PWD" | tr ' ' '-')
DEFAULT_BRANCH="main"
DEFAULT_VISIBILITY="private"
DEFAULT_MSG="Initial commit"

# 解析可选命令行参数: -n 仓库名 -b 分支名 -v (public|private) -m 提交信息
while getopts "n:b:v:m:h" opt; do
  case $opt in
    n) REPO_NAME="$OPTARG" ;;
    b) BRANCH_NAME="$OPTARG" ;;
    v) VISIBILITY="$OPTARG" ;;
    m) COMMIT_MSG="$OPTARG" ;;
    h)
      echo "Usage: $(basename "$0") [-n repo_name] [-b branch] [-v public|private] [-m commit_msg]"
      exit 0
      ;;
    *) exit 1 ;;
  esac
done

echo "=== [1/6] 检查环境与 GitHub 登录状态 ==="
for cmd in git gh; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "❌ 未检测到 $cmd，请先安装后再运行。"
    exit 1
  fi
done

if ! gh auth status >/dev/null 2>&1; then
  echo "⚠️ 当前未登录 GitHub CLI，正在启动登录流程..."
  gh auth login
fi
# 确保 git 可以使用 gh 作为凭据助手
gh auth setup-git >/dev/null 2>&1 || true

GH_USER=$(gh api user --jq '.login')
echo "✅ 已登录账号: $GH_USER"

echo "=== [2/6] 配置仓库名称与远端冲突检测 ==="
if [ -z "$REPO_NAME" ]; then
  read -r -p "请输入 GitHub 仓库名称 [默认: $DEFAULT_NAME]: " REPO_NAME
  REPO_NAME="${REPO_NAME:-$DEFAULT_NAME}"
fi

EXISTING_ACTION="create"
while gh repo view "$GH_USER/$REPO_NAME" >/dev/null 2>&1; do
  echo "⚠️ 远端仓库 '$GH_USER/$REPO_NAME' 已存在！"
  echo "  [1] 重新输入一个新仓库名称"
  echo "  [2] 直接关联该已有仓库并推送到远端"
  echo "  [3] 取消并退出"
  read -r -p "请选择处理方式 [1/2/3, 默认 1]: " conflict_choice
  conflict_choice="${conflict_choice:-1}"

  case "$conflict_choice" in
    1)
      read -r -p "请输入新的仓库名称: " REPO_NAME
      while [ -z "$REPO_NAME" ]; do
        read -r -p "名称不能为空，请重新输入: " REPO_NAME
      done
      ;;
    2)
      EXISTING_ACTION="push_existing"
      break
      ;;
    3)
      echo "已退出。"
      exit 0
      ;;
    *)
      echo "无效选项，请重新选择。"
      ;;
  esac
done

echo "=== [3/6] 配置分支与可见性 ==="
if [ -z "$BRANCH_NAME" ]; then
  # 如果已经是 git 仓库，优先读取当前分支名作为默认值
  if [ -d ".git" ]; then
    CURRENT_BR=$(git branch --show-current 2>/dev/null || true)
    DEFAULT_BRANCH="${CURRENT_BR:-$DEFAULT_BRANCH}"
  fi
  read -r -p "请输入目标分支名称 [默认: $DEFAULT_BRANCH]: " BRANCH_NAME
  BRANCH_NAME="${BRANCH_NAME:-$DEFAULT_BRANCH}"
fi

if [ "$EXISTING_ACTION" = "create" ] && [ -z "$VISIBILITY" ]; then
  read -r -p "请选择仓库可见性 (public / private / internal) [默认: $DEFAULT_VISIBILITY]: " VISIBILITY
  VISIBILITY="${VISIBILITY:-$DEFAULT_VISIBILITY}"
  if [[ ! "$VISIBILITY" =~ ^(public|private|internal)$ ]]; then
    echo "⚠️ 输入无效，默认使用 private。"
    VISIBILITY="private"
  fi
fi

echo "=== [4/6] 初始化本地 Git 仓库 ==="
if [ ! -d ".git" ]; then
  git init
  echo "✅ 已初始化本地 Git 仓库 (.git)"
else
  echo "ℹ️ 当前目录已存在 .git 仓库，跳过 git init"
fi

# 自动兜底 Git 用户名与邮箱，防止首次机器未配置导致 commit 失败
if [ -z "$(git config user.name)" ]; then
  git config user.name "$GH_USER"
fi
if [ -z "$(git config user.email)" ]; then
  GH_ID=$(gh api user --jq '.id')
  git config user.email "${GH_ID}+${GH_USER}@users.noreply.github.com"
fi

# 检查是否存在 .gitignore
if [ ! -f ".gitignore" ]; then
  read -r -p "未检测到 .gitignore，是否生成基础忽略文件？[y/N]: " gen_ignore
  if [[ "$gen_ignore" =~ ^[Yy]$ ]]; then
    cat << 'EOF' > .gitignore
.DS_Store
Thumbs.db
.env
.venv/
venv/
__pycache__/
node_modules/
dist/
build/
*.log
.idea/
.vscode/
EOF
    echo "✅ 已生成基础 .gitignore"
  fi
fi

echo "=== [5/6] 暂存文件与创建提交 ==="
git add -A

# 检查是否有文件可提交；若空目录且无历史提交，自动创建 README.md
if ! git rev-parse --verify HEAD >/dev/null 2>&1 && [ -z "$(git status --porcelain)" ]; then
  echo "⚠️ 当前目录为空，正在生成 README.md 以便完成首次提交..."
  echo "# $REPO_NAME" > README.md
  git add README.md
fi

if [ -n "$(git status --porcelain)" ]; then
  if [ -z "$COMMIT_MSG" ]; then
    read -r -p "请输入 Commit 提交信息 [默认: $DEFAULT_MSG]: " COMMIT_MSG
    COMMIT_MSG="${COMMIT_MSG:-$DEFAULT_MSG}"
  fi
  git commit -m "$COMMIT_MSG"
else
  echo "ℹ️ 工作区无新增改动，将直接推送已有 Commit。"
fi

# 统一设置当前分支名称
git branch -M "$BRANCH_NAME"

echo "=== [6/6] 创建/同步远端仓库并推送 ==="
if [ "$EXISTING_ACTION" = "create" ]; then
  # 如果本地已有旧的 origin remote，先移除以免 gh repo create 报错
  if git remote get-url origin >/dev/null 2>&1; then
    OLD_ORIGIN=$(git remote get-url origin)
    echo "ℹ️ 检测到已存在本地 origin ($OLD_ORIGIN)，正在重置为新仓库地址..."
    git remote remove origin
  fi

  gh repo create "$REPO_NAME" --"$VISIBILITY" --source=. --remote=origin --push
  # 确保目标分支与远端建立追踪关系
  git push -u origin "$BRANCH_NAME"
else
  REMOTE_URL=$(gh repo view "$GH_USER/$REPO_NAME" --json url --jq '.url').git
  if git remote get-url origin >/dev/null 2>&1; then
    git remote set-url origin "$REMOTE_URL"
  else
    git remote add origin "$REMOTE_URL"
  fi
  echo "正在推送到已有仓库 $REMOTE_URL (分支: $BRANCH_NAME)..."
  if ! git push -u origin "$BRANCH_NAME"; then
    read -r -p "⚠️ 远端包含本地没有的提交，是否执行 --force 覆盖推送？[y/N]: " force_push
    if [[ "$force_push" =~ ^[Yy]$ ]]; then
      git push -u origin "$BRANCH_NAME" --force
    else
      echo "已取消强制推送，请手动执行 git pull --rebase origin $BRANCH_NAME 后再推送。"
      exit 1
    fi
  fi
fi

echo "🎉 上传完成！仓库地址: https://github.com/$GH_USER/$REPO_NAME (分支: $BRANCH_NAME)"
