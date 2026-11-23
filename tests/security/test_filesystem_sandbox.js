/**
 * Security test: File system tool sandboxing
 * 
 * Tests cover:
 * - Path traversal prevention
 * - isInside() boundary checks
 * - Filename sanitization
 * - Database storage path safety
 */

const { isInside, sanitizeFileName } = require('../app/core/PathSecurity.js');

describe('File System Sandboxing', () => {
  test('isInside prevents path traversal above root', () => {
    const root = '/home/user/app data';
    // Inside the root - should pass
    expect(isInside(root, '/home/user/app data/models/file.txt')).toBe(true);
    // Outside the root - should fail
    expect(isInside(root, '/home/user/other/file.txt')).toBe(false);
    // Directly above root - should fail (boundary check)
    expect(isInside(root, '/home/user/app datafile.txt')).toBe(false);
  });

  test('isInside with trailing slash boundary', () => {
    const root = '/home/user/app data';
    // Boundary: path must have / after root length
    expect(isInside(root, '/home/user/app data/')).toBe(true);
    // Without trailing slash, extra chars should fail
    expect(isInside(root, '/home/user/app dat')).toBe(false);
  });

  test('sanitizeFileName strips path separators and traversal', () => {
    expect(sanitizeFileName('normal.txt')).toBe('normal.txt');
    expect(sanitizeFileName('path/with/slashes.txt')).toBe('path_with_slashes.txt');
    expect(sanitizeFileName('path\\with\\backslashes.txt')).toBe('path_with_backslashes.txt');
    expect(sanitizeFileName('..\\..\\escape.txt')).toBe('_ _escape.txt'); // .. replaced with __
    expect(sanitizeFileName('../escape.txt')).toBe('_escape.txt');
    expect(sanitizeFileName('normal..name.txt')).toBe('normal__name.txt');
  });

  test('database path uses secure base directory', () => {
    // The database path should be under AppDataLocation, not current dir
    const dbPath = require('../app/core/Paths.js').databasePath();
    const home = require('os').homedir();
    // Should not be in current working directory or home directly
    expect(dbPath).not.toContain('/modelfit.db' /* relative path */);
    // Should be under appropriate platform dir
    expect(dbPath).toMatch(/^[A-Za-z]:\\|\\//); // absolute path
  });

  test('config path is under app data directory', () => {
    const configPath = require('../app/core/Paths.js').configPath();
    // Should be under writable location, not in source tree
    expect(configPath).not.toMatch(/\.modelfit\/config\.json$/);
  });
});