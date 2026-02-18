#!/bin/bash
# Git双仓库一键推送全局配置脚本
# 适配Linux系统（你的lubancat），配置完成后所有工程通用

# 颜色输出（方便看结果）
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # 重置颜色

echo -e "${YELLOW}===== 开始配置Git全局双推送别名 =====${NC}"

# 第一步：让用户选择分支名（master/main，适配不同仓库）
read -p "请输入你的默认分支名（默认master，直接回车用master，输入main则用main）：" BRANCH
BRANCH=${BRANCH:-master} # 默认用master

# 第二步：配置全局pushall别名（所有工程通用）
echo -e "${YELLOW}1. 配置全局git pushall别名（分支：$BRANCH）...${NC}"
git config --global alias.pushall "!git push origin $BRANCH && git push github $BRANCH"

# 验证别名是否配置成功
ALIAS_CHECK=$(git config --global --get alias.pushall)
if [ -n "$ALIAS_CHECK" ]; then
    echo -e "${GREEN}✅ 全局pushall别名配置成功！${NC}"
else
    echo -e "${RED}❌ 别名配置失败，请手动执行：git config --global alias.pushall '!git push origin $BRANCH && git push github $BRANCH'${NC}"
    exit 1
fi

# 第三步：可选 - 给当前工程添加GitHub远程（用户可选是否添加）
read -p "是否给当前工程添加GitHub远程仓库？(y/n，默认n)：" ADD_GITHUB
if [ "$ADD_GITHUB" = "y" ] || [ "$ADD_GITHUB" = "Y" ]; then
    read -p "请输入你的GitHub用户名（比如huan26560）：" GITHUB_USER
    read -p "请输入你的GitHub仓库名（比如RK3576-expansion）：" GITHUB_REPO
    GITHUB_URL="git@github.com:$GITHUB_USER/$GITHUB_REPO.git"
    
    echo -e "${YELLOW}2. 给当前工程添加GitHub远程：$GITHUB_URL...${NC}"
    git remote add github $GITHUB_URL
    
    # 验证远程是否添加成功
    REMOTE_CHECK=$(git remote -v | grep github | grep push)
    if [ -n "$REMOTE_CHECK" ]; then
        echo -e "${GREEN}✅ GitHub远程添加成功！${NC}"
    else
        echo -e "${RED}❌ GitHub远程添加失败，请检查用户名/仓库名是否正确${NC}"
    fi
fi

# 第四步：输出使用说明
echo -e "\n${GREEN}===== 配置完成！使用说明 =====${NC}"
echo -e "1. 任何工程下，完成本地提交后，执行：${YELLOW}git pushall${NC} → 一键推origin+github"
echo -e "2. 完整流程（改代码后）："
echo -e "   - git add . （暂存所有更改）"
echo -e "   - git commit -m \"你的备注\" （本地提交）"
echo -e "   - git pushall （一键双推）"
echo -e "3. 如果想在VS Code里点按钮：按Ctrl+Shift+B → 选「全局-提交并推双仓库」"

echo -e "\n${YELLOW}⚠️  注意：如果分支名不是$BRANCH，需重新配置别名，执行：${NC}"
echo -e "git config --global alias.pushall '!git push origin 你的分支名 && git push github 你的分支名'"