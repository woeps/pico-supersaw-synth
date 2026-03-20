name: SuperSaw MIDI Synth Documentation

# Docsify Configuration
This directory contains the documentation for the SuperSaw MIDI Synth project, served using Docsify.

## Local Development

To run the documentation locally:

1. Install Docsify CLI globally:
   ```bash
   npm i docsify-cli -g
   ```

2. Navigate to the docs directory:
   ```bash
   cd docs
   ```

3. Start the local server:
   ```bash
   docsify serve
   ```

4. Open your browser and navigate to `http://localhost:3000`

## Deployment

The documentation is automatically deployed to GitHub Pages when changes are pushed to the main/master branch. The deployment is handled by the GitHub Actions workflow in `.github/workflows/deploy-docs.yml`.

### Manual Deployment

To manually trigger a deployment:

1. Go to the Actions tab in your GitHub repository
2. Select "Deploy Docs to GitHub Pages" workflow
3. Click "Run workflow"

## Customization

- **Theme**: Vue theme (can be changed in index.html)
- **Sidebar**: Auto-generated from _sidebar.md
- **Search**: Full-text search enabled
- **Code Copy**: Copy code snippets with one click
- **Pagination**: Navigate between pages easily
- **Syntax Highlighting**: C, C++, and CMake support

## Adding New Documentation

1. Create new markdown files in the docs directory
2. Update _sidebar.md to include the new page
3. Follow the existing naming conventions
4. Use proper markdown formatting and headers

## Resources

- [Docsify Documentation](https://docsify.js.org/)
- [GitHub Pages Documentation](https://docs.github.com/en/pages)
